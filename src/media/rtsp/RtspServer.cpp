#include <vector>
#include <thread>
#include <string.h>
#include <mutex>
#include <fstream>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <algorithm>
#include "Common.h"
#include "Logger.h"
#include "RtspServer.h"
#include "rtsp.h"
#include "DayNightSwitch.h"
#include "IVideo.h"
#include "HalProvider.h"
#include "MediaSession.h"
#include "VideoSource.h"
#include "AudioSource.h"

using namespace media;

namespace {

void logRtspStreamInfo(const char* label, const hal::VideoStreamInfo& info) {
    Logger::log(LogLevel::INFO,
                "%s: index=%d sensor=%d stream=%d enabled=%d size=%dx%d fps=%d/%d payload=%d",
                label,
                info.index,
                info.sensor_index,
                info.output_index,
                info.enabled,
                info.width,
                info.height,
                info.fps_num,
                info.fps_den,
                static_cast<int>(info.payload));
}

}

bool RtspServer::extractSpsPps(const uint8_t* h264Data, size_t dataSize, std::vector<uint8_t>& sps, std::vector<uint8_t>& pps)
{
    if (!h264Data || dataSize < 4) {
        Logger::log(LogLevel::ERROR, "Invalid H264 data or size");
        return false;
    }

    const uint8_t* dataEnd = h264Data + dataSize;
    const uint8_t* pos = h264Data;

    // Define NALU start codes
    const uint8_t startCode3[] = {0x00, 0x00, 0x01};
    const uint8_t startCode4[] = {0x00, 0x00, 0x00, 0x01};

    bool foundSps = false;
    bool foundPps = false;

    while (pos < dataEnd - 3) {
        // Check for 4-byte start code
        if (memcmp(pos, startCode4, 4) == 0) {
            pos += 4;
        }
        // Check for 3-byte start code
        else if (memcmp(pos, startCode3, 3) == 0) {
            pos += 3;
        }
        else {
            pos++;
            continue;
        }

        // Check if we have enough data for NALU header
        if (pos >= dataEnd) {
            break;
        }

        // Get NALU type (first 5 bits of the header)
        uint8_t naluType = pos[0] & 0x1F;

        // Find next start code to determine NALU length
        const uint8_t* nextStart = nullptr;
        const uint8_t* searchPos = pos + 1;

        while (searchPos < dataEnd - 3) {
            if (memcmp(searchPos, startCode4, 4) == 0) {
                nextStart = searchPos;
                break;
            }
            else if (memcmp(searchPos, startCode3, 3) == 0) {
                nextStart = searchPos;
                break;
            }
            searchPos++;
        }

        // If no next start code found, use end of data
        size_t naluLength = (nextStart) ? (nextStart - pos) : (dataEnd - pos);

        // Process SPS (type 7)
        if (naluType == 7) {
            sps.assign(pos, pos + naluLength);
            foundSps = true;
            Logger::log(LogLevel::DEBUG, "Found SPS, length: %zu", naluLength);
        }
        // Process PPS (type 8)
        else if (naluType == 8) {
            pps.assign(pos, pos + naluLength);
            foundPps = true;
            Logger::log(LogLevel::DEBUG, "Found PPS, length: %zu", naluLength);
        }

        // If we found both SPS and PPS, we can exit early
        if (foundSps && foundPps) {
            break;
        }

        // Move to next NALU
        if (nextStart) {
            pos = nextStart;
        } else {
            break;
        }
    }

    if (!foundSps) {
        Logger::log(LogLevel::WARNING, "SPS not found in H264 stream");
    }
    if (!foundPps) {
        Logger::log(LogLevel::WARNING, "PPS not found in H264 stream");
    }

    // Return true if at least one of SPS or PPS was found
    return foundSps || foundPps;
};
std::function<void(void)> RtspServer::onSessionClosedCallback = nullptr;
using namespace media;

std::shared_ptr<RtspServer> RtspServer::getInstance()
{
	static std::shared_ptr<RtspServer> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new RtspServer()); });
	return instance;
}

RtspServer::RtspServer()
    : pullFrameThread(nullptr)
	, pullFrameThreadRun(false)
	, alreadyGetSpsPps(false)
    , port_(DEFAULT_RTSP_PORT)
    , enableAudio_(true)
    , streamingEnabled_(false)
{
    initialized = initialize();
}

void RtspServer::setPort(int port)
{
    if (port > 0 && port <= 65535) {
        port_ = port;
    }
}

RtspServer::~RtspServer()
{
	stop();
    deinitialize();
}

bool RtspServer::initialize()
{
#ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "RTSP initialize: BUILD_FOR_SIMULATION=ON (HAL provider expected: SimVideo/SimAudio)");
#else
    Logger::log(LogLevel::INFO, "RTSP initialize: BUILD_FOR_SIMULATION=OFF (HAL provider expected: IngenicVideo/IngenicAudio)");
#endif

    if (!initVideo()) {
        Logger::log(LogLevel::ERROR, "Video init failed");
        return false;
    }

    if (enableAudio_) {
        if (!initAudio()) {
            Logger::log(LogLevel::ERROR, "Audio init failed, continue without audio");
            return false;
        }
    }
    Logger::log(LogLevel::DEBUG, "Video init success");
    return true;
}

void RtspServer::deinitialize()
{
    if (initialized) {
        if (!uninitVideo()) {
            Logger::log(LogLevel::ERROR, "Video uninit failed");
        }
        if (!uninitAudio()) {
            Logger::log(LogLevel::ERROR, "Audio uninit failed");
        }
    }
}

bool RtspServer::stop()
{
	if (this->pullFrameThread) {
		this->pullFrameThreadRun = false;
		if (this->pullFrameThread && this->pullFrameThread->joinable()) {
			this->pullFrameThread->join();
		}
		this->pullFrameThread = nullptr;
	}

    if (this->videoSession_) {
        this->videoSession_->stop();
    }
    uninitAudio();

	if (this->rtsp_server) {
		stop_server(this->rtsp_server);
		destroy_server(this->rtsp_server);
		this->rtsp_server = nullptr;
	}
	
	return true;
}

bool RtspServer::start()
{
	
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "RtspServer is not initialized");
        return false;
    }

	//day-night switch
	daynight_switch(true);
    this->pullFrameThreadRun = true;
    this->pullFrameThread = std::make_shared<std::thread>([this]() {
        start_internal();
    });

    return true;
}

int RtspServer::onSessionClosed(void **data, size_t *size, uint64_t *timestamp)
{
	Logger::log(LogLevel::INFO, "onSessionClosed");
    RtspServer *server = RtspServer::getInstance().get();
    if (server) {
        // Keep media sessions alive across client reconnects. We only stop
        // streaming here so the next PLAY can restart the existing sessions.
        if (server->videoSession_) {
            server->videoSession_->stop();
        }
        if (server->audioSession_) {
            server->audioSession_->stop();
        }
        server->streamingEnabled_ = false;
    }
	if (onSessionClosedCallback) {
		onSessionClosedCallback();
	}
	return 0;
}

int RtspServer::pullFrame(void **data, size_t *size, uint64_t *timestamp)
{
    auto instance = RtspServer::getInstance();
    if (!instance || !instance->videoSession_) {
        return -1;
    }
    return MediaSession::pullFrame(data, size, timestamp, instance->videoSession_.get());
}

int RtspServer::releaseFrame(void **data, size_t *size, uint64_t *timestamp)
{
    auto instance = RtspServer::getInstance();
    if (!instance || !instance->videoSession_) {
        return -1;
    }
    return MediaSession::releaseFrame(data, size, timestamp, instance->videoSession_.get());
}

int RtspServer::pullAudioFrame(void **data, size_t *size, uint64_t *timestamp)
{
    auto instance = RtspServer::getInstance();
    if (!instance || !instance->audioSession_) {
        return -1;
    }
    return MediaSession::pullFrame(data, size, timestamp, instance->audioSession_.get());
}

int RtspServer::releaseAudioFrame(void **data, size_t *size, uint64_t *timestamp)
{
    auto instance = RtspServer::getInstance();
    if (!instance || !instance->audioSession_) {
        return -1;
    }
    return MediaSession::releaseFrame(data, size, timestamp, instance->audioSession_.get());
}

bool RtspServer::start_internal()
{
    if (!videoSession_) {
        Logger::log(LogLevel::ERROR, "Video session is not ready");
        return false;
    }
    MediaParams vParams = videoSession_->getParams();
    int fps = vParams.videoFrameRate;
    struct rtsp_server_param rtsp_server_param = {0};
    rtsp_server_param.port = port_;
    Logger::log(LogLevel::DEBUG, "rtsp_server_param.port = %d", rtsp_server_param.port);
    rtsp_server_param.video_enable = 1;
    rtsp_server_param.video_fps = fps;
    if (vParams.videoCodec == VideoCodec::H264) {
        rtsp_server_param.video_codec = CODEC_H264;
    } else if (vParams.videoCodec == VideoCodec::H265) {
        rtsp_server_param.video_codec = CODEC_H265;
    }
    rtsp_server_param.video_sample_rate = 90000;
    rtsp_server_param.video_stream_id = 1;
    rtsp_server_param.audio_enable = (enableAudio_ && audioSession_) ? 1 : 0;
    rtsp_server_param.audio_sample_rate = audioSampleRate_;
    rtsp_server_param.audio_stream_id = 0;
    rtsp_server_param.audio_samples_per_packet = audioNumPerFrame_;
    rtsp_server_param.audio_codec = AUDIO_CODEC_PCMA;
    rtsp_server_param.audio_channels = 1;

    std::vector<uint8_t> pre_sps;
    std::vector<uint8_t> pre_pps;
    if (videoSession_ && videoSession_->start()) {
        Logger::log(LogLevel::INFO, "Preopen video session for SDP");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!alreadyGetSpsPps && std::chrono::steady_clock::now() < deadline) {
            void* d = nullptr;
            size_t sz = 0;
            uint64_t ts = 0;
            int r = MediaSession::pullFrame(&d, &sz, &ts, videoSession_.get());
            if (r == 0 && d && sz > 0) {
                std::vector<uint8_t> sps, pps;
                if (extractSpsPps(static_cast<uint8_t*>(d), sz, sps, pps) && !sps.empty() && !pps.empty()) {
                    alreadyGetSpsPps = true;
                    pre_sps = std::move(sps);
                    pre_pps = std::move(pps);
                    Logger::log(LogLevel::INFO, "Preopen extracted SPS=%zu, PPS=%zu", pre_sps.size(), pre_pps.size());
                }
                MediaSession::releaseFrame(&d, &sz, nullptr, videoSession_.get());
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        videoSession_->stop();
        Logger::log(LogLevel::INFO, "Preopen video session finished");
    }

    this->rtsp_server = create_server(&rtsp_server_param);
    if (!this->rtsp_server) {
        return false;
    }

    if (alreadyGetSpsPps && !pre_sps.empty() && !pre_pps.empty()) {
        set_server_param(this->rtsp_server, RTSP_SERVER_PARAM_VIDEO_SPS, pre_sps.data(), pre_sps.size());
        set_server_param(this->rtsp_server, RTSP_SERVER_PARAM_VIDEO_PPS, pre_pps.data(), pre_pps.size());
        Logger::log(LogLevel::INFO, "Injected SPS/PPS to RTSP: sps=%zu, pps=%zu", pre_sps.size(), pre_pps.size());
    }

    register_function(this->rtsp_server, FUNC_ID_PULL_VIDEO_FRAME, RtspServer::pullFrame);
    register_function(this->rtsp_server, FUNC_ID_RELEASE_VIDEO_FRAME, RtspServer::releaseFrame);
    register_function(this->rtsp_server, FUNC_ID_PULL_AUDIO_FRAME, RtspServer::pullAudioFrame);
    register_function(this->rtsp_server, FUNC_ID_RELEASE_AUDIO_FRAME, RtspServer::releaseAudioFrame);
    register_function(this->rtsp_server, FUNC_ID_ON_SESSION_CLOSED, RtspServer::onSessionClosed);
    register_function(this->rtsp_server, FUNC_ID_ON_SESSION_PLAY, RtspServer::onSessionPlay);
    start_server(this->rtsp_server);
    Logger::log(LogLevel::INFO, "RTSP server started on port %d", rtsp_server_param.port);

    while (this->pullFrameThreadRun) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return true;

}

int RtspServer::onSessionPlay(void **data, size_t *size, uint64_t *timestamp)
{
    RtspServer *server = RtspServer::getInstance().get();
    if (server) {
        server->streamingEnabled_ = true;
        if (!server->videoSession_) {
            Logger::log(LogLevel::WARNING, "onSessionPlay: video session missing, reinitializing");
            if (!server->initVideo()) {
                Logger::log(LogLevel::ERROR, "onSessionPlay: failed to reinitialize video session");
                return -1;
            }
        }
        if (server->enableAudio_ && !server->audioSession_) {
            Logger::log(LogLevel::WARNING, "onSessionPlay: audio session missing, reinitializing");
            if (!server->initAudio()) {
                Logger::log(LogLevel::ERROR, "onSessionPlay: failed to reinitialize audio session");
            }
        }
        if (server->videoSession_ && !server->videoSession_->isRunning()) {
            if (server->videoSession_->start()) {
                Logger::log(LogLevel::INFO, "onSessionPlay: video session started");
                server->videoSession_->requestIDR();
                Logger::log(LogLevel::INFO, "onSessionPlay: requested IDR");
            } else {
                Logger::log(LogLevel::ERROR, "onSessionPlay: failed to start video session");
                return -1;
            }
        }
        if (server->audioSession_ && !server->audioSession_->isRunning()) {
            if (server->audioSession_->start()) {
                Logger::log(LogLevel::INFO, "onSessionPlay: audio session started");
            } else {
                Logger::log(LogLevel::ERROR, "onSessionPlay: failed to start audio session");
            }
        }
    }
    return 0;
}

bool RtspServer::initVideo()
{
    video_ = hal::HalProvider::createVideo();
    if (!video_) {
        Logger::log(LogLevel::ERROR, "initialize: createVideo failed");
        return false;
    }
    if (!video_->init()) {
        Logger::log(LogLevel::ERROR, "initialize: video init failed");
        return false;
    }
    auto stream = video_->createVideoStream();
    if (!stream) {
        Logger::log(LogLevel::ERROR, "initialize: createVideoStream failed");
        return false;
    }
    hal::VideoStreamConfig cfg;
    memset(&cfg, 0, sizeof(hal::VideoStreamConfig));
    cfg.payload = hal::VideoPayloadType::H264;
    cfg.channel.sensor_index = RTSP_SENSOR_ID;
    cfg.channel.stream_index = RTSP_STREAM_ID;
    cfg.width = 1280;
    cfg.height = 720;
    cfg.fps_num = 30;
    cfg.fps_den = 1;
    cfg.rc_mode = hal::VideoRcMode::CBR;
    cfg.enable_ivdc = true;
    Logger::log(LogLevel::INFO,
                "rtsp config: sensor=%d stream=%d requested_size=%dx%d requested_fps=%d/%d codec=%d rc=%d",
                cfg.channel.sensor_index,
                cfg.channel.stream_index,
                cfg.width,
                cfg.height,
                cfg.fps_num,
                cfg.fps_den,
                static_cast<int>(cfg.payload),
                static_cast<int>(cfg.rc_mode));
    if (!stream->configure(cfg)) {
        Logger::log(LogLevel::ERROR, "initialize: stream configure failed");
        return false;
    }
    hal::VideoStreamInfo info{};
    if (stream->getInfo(info)) {
        logRtspStreamInfo("rtsp stream info", info);
    } else {
        Logger::log(LogLevel::WARNING, "rtsp stream info: query failed");
    }
    auto videoSource = std::make_shared<VideoSource>(stream);
    videoSession_ = std::make_shared<MediaSession>(videoSource, 60);
    return true;
}

bool RtspServer::uninitVideo(void)
{
    if (initialized) {
        if (videoSession_) {
            videoSession_->stop();
            videoSession_.reset();
        }
        if (video_) {
            video_->exit();
            video_.reset();
        }
    }
    return true;
}

bool RtspServer::initAudio()
{
    auto audio = hal::HalProvider::createAudio();
    if (!audio) {
        return false;
    }
    if (!audio->init()) {
        return false;
    }
    auto audioStream = audio->createAudioStream();
    if (!audioStream) {
        audio->exit();
        return false;
    }
    hal::AudioStreamConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.payload = hal::AudioPayloadType::G711A; // 使用 PCMA (G.711A-law) 编码，8000Hz 是最通用的配置
    cfg.channel.device_index = 1;
    cfg.channel.channel_index = 0;
    cfg.sample_rate = 8000;
    cfg.channels = 1;
    cfg.bit_width = 16;
    cfg.num_per_frame = 320; // 40ms @ 8000Hz
    cfg.frame_num = 10;
    cfg.volume = 80;
    cfg.gain = 28;
    cfg.bitrate_per_channel = 64000; // 8kHz * 8bit = 64kbps
    cfg.quality = 0;
    cfg.input = hal::AudioInputType::AI;
    if (!audioStream->configure(cfg)) {
        audio->exit();
        return false;
    }
    audioSampleRate_ = cfg.sample_rate;
    audioNumPerFrame_ = cfg.num_per_frame;
    int audioFrameDurationUs = 0;
    if (cfg.sample_rate > 0 && cfg.num_per_frame > 0) {
        audioFrameDurationUs = (1000000 * cfg.num_per_frame) / cfg.sample_rate;
    }
    int audioPollTimeoutMs = 20;
    if (audioFrameDurationUs > 0) {
        int frameMs = audioFrameDurationUs / 1000;
        audioPollTimeoutMs = std::max(5, std::min(50, frameMs));
    }
    Logger::log(LogLevel::INFO,
                "Audio pacing config: frame_duration_us=%d, poll_timeout_ms=%d",
                audioFrameDurationUs, audioPollTimeoutMs);

    auto audioSource = std::make_shared<AudioSource>(audioStream, audioPollTimeoutMs, audioFrameDurationUs);
    audioSession_ = std::make_shared<MediaSession>(audioSource, 80);
    return true;
}

bool RtspServer::uninitAudio()
{
    if (audioSession_) {
        audioSession_->stop();
        audioSession_.reset();
    }

    return true;
}

void RtspServer::registerOnsessionClosedCallback(std::function<void(void)> callback)
{
    onSessionClosedCallback = callback;
}

bool RtspServer::isRunning(void)
{
    return !!is_server_running(this->rtsp_server);
}

bool RtspServer::daynight_switch(bool on)
{
    auto daynight_controller = DayNightSwitch::getInstance();
    if (!daynight_controller) {
        return false;
    }

    daynight_controller->setCdsPins(CDS_SENSOR_PIN);
    daynight_controller->setIRLedPins(IR_LED_PIN);
    daynight_controller->setIRCutPins(IR_CUT_ENABLE_PIN, IR_CUT_CTRL_PIN);

    if (on) {
        auto daynight_state = daynight_controller->getDayNightState();
        daynight_controller->controlISP(daynight_state);
        daynight_controller->controlIRCut(daynight_state);
        daynight_controller->controlIRLed(daynight_state);
		daynight_controller->startAutoSwithch();
    } else {
        daynight_controller->controlISP(DayNightState::DAY);
        daynight_controller->controlIRCut(DayNightState::DAY);
        daynight_controller->controlIRLed(DayNightState::DAY);
		daynight_controller->suspendAutoSwitch();
    }

    return true;
}
