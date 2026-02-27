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

using namespace media;

frame_fifo_t RtspServer::frame_fifo = {0};

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
size_t RtspServer::frame_buffer_size = 0;
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
    , enableAudio_(true)
    , streamingEnabled_(false)
{
    initialized = initialize();
}

RtspServer::~RtspServer()
{
	stop();
    deinitialize();
}

bool RtspServer::initialize()
{
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

    if (this->stream_) {
        this->stream_->stop();
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

    /* Step.6 Get stream */
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
        server->uninitAudio();
        server->audioRecording_ = false;
        server->streamingEnabled_ = false;
    }
	if (onSessionClosedCallback) {
		onSessionClosedCallback();
	}
	return 0;
}

int RtspServer::pullFrame(void **data, size_t *size, uint64_t *timestamp)
{
    if (frame_fifo.count == 0) {
        return -1;
    }
	
    frame_buffer_t *frame = &frame_fifo.frames[frame_fifo.head];
    *data = frame->buffer;
    *size = frame->used_size;
    if (timestamp) {
        *timestamp = frame->pts;
    }

	Logger::log(LogLevel::DEBUG, "Frame retrieved from FIFO, remaining: %d", frame_fifo.count);

    return 0;
}

int RtspServer::releaseFrame(void **data, size_t *size, uint64_t *timestamp)
{
    if (frame_fifo.count == 0) {
        return -1;
    }

	{
		std::unique_lock<std::mutex> lock(frame_fifo.mutex);
		frame_fifo.head = (frame_fifo.head + 1) % FIFO_MAX_FRAMES;
		frame_fifo.count--;
	}

    frame_fifo.not_full.notify_one();

    return 0;
}

int RtspServer::pullAudioFrame(void **data, size_t *size, uint64_t *timestamp)
{
    RtspServer *server = RtspServer::getInstance().get();
    if (!server) return -1;
    if (server->audioRecorder_ && !server->audioRecording_) {
        if (server->audioRecorder_->start()) {
            server->audioRecording_ = true;
        } else {
            return -1;
        }
    }
    std::lock_guard<std::mutex> lock(server->audioDataMutex_);
    if (server->audioDataQueue_.empty()) {
        return -1;
    }
    const AudioQueued &front = server->audioDataQueue_.front();
    server->audioWorkBuf_.assign(front.data.begin(), front.data.end());
    server->audioDataQueue_.pop();
    *data = server->audioWorkBuf_.empty() ? nullptr : server->audioWorkBuf_.data();
    *size = server->audioWorkBuf_.size();
    if (timestamp) {
        *timestamp = front.timestamp_ms;
    }
    return (*data && *size) ? 0 : -1;
}

int RtspServer::releaseAudioFrame(void **data, size_t *size, uint64_t *timestamp)
{
    (void)timestamp;
    RtspServer *server = RtspServer::getInstance().get();
    if (!server) return -1;
    if (data) *data = nullptr;
    if (size) *size = 0;
    return 0;
}

void RtspServer::staticAudioDataCallback(const uint8_t* data, size_t size, uint64_t timestamp, bool isKeyFrame, void* userData)
{
    RtspServer *server = static_cast<RtspServer *>(userData);
    if (!server || !data || size == 0) {
        return;
    }
    std::vector<uint8_t> buf(size);
    memcpy(buf.data(), data, size);
    {
        std::lock_guard<std::mutex> lock(server->audioDataMutex_);
        server->audioDataQueue_.push(AudioQueued{std::move(buf), timestamp});
    }
}

bool RtspServer::start_internal()
{
    if (!stream_) {
        Logger::log(LogLevel::ERROR, "HAL stream is not ready");
        return false;
    }
    hal::VideoStreamInfo info{};
    if (!stream_->getInfo(info)) {
        Logger::log(LogLevel::WARNING, "stream info: query failed");
    }
    int fps = (info.fps_den > 0) ? (info.fps_num / info.fps_den) : 15;
    struct rtsp_server_param rtsp_server_param = {0};
    rtsp_server_param.port = 8554;
    Logger::log(LogLevel::DEBUG, "rtsp_server_param.port = %d", rtsp_server_param.port);
    //video
    rtsp_server_param.video_enable = 1;
    rtsp_server_param.video_fps = fps;
	if (info.payload == hal::VideoPayloadType::H264) {
        rtsp_server_param.video_codec = CODEC_H264;
	} else if (info.payload == hal::VideoPayloadType::H265) {
	    rtsp_server_param.video_codec = CODEC_H265;
	}
    rtsp_server_param.video_sample_rate = 90000;
    rtsp_server_param.video_stream_id = 1;
    rtsp_server_param.audio_enable = (enableAudio_ && audioRecorder_) ? 1 : 0;
    rtsp_server_param.audio_sample_rate = audioSampleRate_;
    rtsp_server_param.audio_stream_id = 0;
    rtsp_server_param.audio_samples_per_packet = audioNumPerFrame_;
    rtsp_server_param.audio_codec = AUDIO_CODEC_PCMU;
    rtsp_server_param.audio_channels = 1;
    this->rtsp_server = create_server(&rtsp_server_param);
    if (!this->rtsp_server) {
        stream_->stop();
        return false;
    }
	
    frame_fifo.head = 0;
    frame_fifo.tail = 0;
    frame_fifo.count = 0;

    for (int i = 0; i < FIFO_MAX_FRAMES; i++) {
        frame_fifo.frames[i].buffer = malloc(DEFAULT_FRAME_BUFFER_SIZE);
        if (!frame_fifo.frames[i].buffer) {
            Logger::log(LogLevel::ERROR, "Failed to preallocate frame buffer %d", i);
            for (int j = 0; j < i; j++) {
                free(frame_fifo.frames[j].buffer);
                frame_fifo.frames[j].buffer = nullptr;
            }
            stream_->stop();
            return false;
        }
        frame_fifo.frames[i].buffer_size = DEFAULT_FRAME_BUFFER_SIZE;
    }
    Logger::log(LogLevel::DEBUG, "Preallocated %d frame buffers of size %zu bytes", FIFO_MAX_FRAMES, DEFAULT_FRAME_BUFFER_SIZE);

    register_function(this->rtsp_server, FUNC_ID_PULL_VIDEO_FRAME, RtspServer::pullFrame);
	register_function(this->rtsp_server, FUNC_ID_RELEASE_VIDEO_FRAME, RtspServer::releaseFrame);
    register_function(this->rtsp_server, FUNC_ID_PULL_AUDIO_FRAME, RtspServer::pullAudioFrame);
    register_function(this->rtsp_server, FUNC_ID_RELEASE_AUDIO_FRAME, RtspServer::releaseAudioFrame);
	register_function(this->rtsp_server, FUNC_ID_ON_SESSION_CLOSED, RtspServer::onSessionClosed);
    register_function(this->rtsp_server, FUNC_ID_ON_SESSION_PLAY, RtspServer::onSessionPlay);
    start_server(this->rtsp_server);
	
    bool streamStarted = false;
    while (this->pullFrameThreadRun) {
        if (!streamingEnabled_) {
            if (streamStarted) {
                stream_->stop();
                streamStarted = false;
                {
                    std::lock_guard<std::mutex> lock(frame_fifo.mutex);
                    frame_fifo.head = 0;
                    frame_fifo.tail = 0;
                    frame_fifo.count = 0;
                    frame_fifo.not_empty.notify_all();
                    frame_fifo.not_full.notify_all();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (!streamStarted) {
            if (!stream_->start()) {
                Logger::log(LogLevel::ERROR, "Start HAL stream failed");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            streamStarted = true;
        }
    	/* Polling stream, set timeout as 1000msec */
        if (!stream_->polling(1000)) {
            Logger::log(LogLevel::ERROR, "stream_->polling(1000) timeout");
            continue;
        }
		
        hal::VideoEncodedFrame frame;
        if (!stream_->getFrame(frame)) {
            Logger::log(LogLevel::ERROR, "getFrame failed");
            continue;
        }

        size_t total_length = 0;
        for (int i = 0; i < (int)frame.piece_count; i++) {
            total_length += frame.pieces[i].size;
        }
		
        while (frame_fifo.count >= FIFO_MAX_FRAMES) {
            Logger::log(LogLevel::DEBUG, "Frame FIFO full, dropping oldest to reduce latency");
            std::unique_lock<std::mutex> lock(frame_fifo.mutex);
            frame_fifo.head = (frame_fifo.head + 1) % FIFO_MAX_FRAMES;
            frame_fifo.count--;
        }

        int current_tail = frame_fifo.tail;
        void *frame_buffer = frame_fifo.frames[current_tail].buffer;
        size_t buffer_size = frame_fifo.frames[current_tail].buffer_size;

        if (total_length > buffer_size) {
            Logger::log(LogLevel::INFO, "Frame size %zu exceeds buffer size %zu, reallocating", total_length, buffer_size);
            void *new_buffer = realloc(frame_buffer, total_length);
            if (!new_buffer) {
                Logger::log(LogLevel::ERROR, "realloc failed for frame data");
                stream_->releaseFrame(frame);
                continue;
            }
            frame_buffer = new_buffer;
			{
				std::unique_lock<std::mutex> lock(frame_fifo.mutex);
				frame_fifo.frames[current_tail].buffer = frame_buffer;
				frame_fifo.frames[current_tail].buffer_size = total_length;
			}
        }
		
        size_t offset = 0;
        for (int i = 0; i < (int)frame.piece_count; i++) {
            size_t datasize = frame.pieces[i].size;
            uint8_t *inputData = (uint8_t*)frame.pieces[i].data;
            memcpy((uint8_t*)frame_buffer + offset, inputData, datasize);
            offset += datasize;
        }
		frame_fifo.frames[current_tail].used_size = total_length;
        frame_fifo.frames[current_tail].pts = frame.pts;
#if PPS_SPS_IN_SDP
		std::vector<uint8_t> sps, pps;
		if (!alreadyGetSpsPps && info.payload == hal::VideoPayloadType::H264 && extractSpsPps(static_cast<uint8_t*>(frame_buffer), total_length, sps, pps)) {
			alreadyGetSpsPps = true;
			uint8_t *sps_data = (uint8_t*)malloc(sps.size());
			uint8_t *pps_data = (uint8_t*)malloc(pps.size());
			for (int i=0; i < (int)sps.size(); i++) {
				sps_data[i] = sps[i];
				Logger::log(LogLevel::DEBUG, "sps_data[%d] = 0x%02x", i, sps_data[i]);
			}
			for (int i=0; i < (int)pps.size(); i++) {
				pps_data[i] = pps[i];
				Logger::log(LogLevel::DEBUG, "pps_data[%d] = 0x%02x", i, pps_data[i]);
			}
			set_server_param(this->rtsp_server, RTSP_SERVER_PARAM_VIDEO_SPS, sps_data, sps.size());
			set_server_param(this->rtsp_server, RTSP_SERVER_PARAM_VIDEO_PPS, pps_data, pps.size());
		}
#endif
		{
			std::unique_lock<std::mutex> lock(frame_fifo.mutex);
			frame_fifo.tail = (frame_fifo.tail + 1) % FIFO_MAX_FRAMES;
			frame_fifo.count++;
		}

        Logger::log(LogLevel::DEBUG, "Frame added to FIFO, count: %d", frame_fifo.count);

        frame_fifo.not_empty.notify_one();
        stream_->releaseFrame(frame);
    }
exit_loop:
	
    stream_->stop();

    {
        std::lock_guard<std::mutex> lock(frame_fifo.mutex);
        for (int i = 0; i < FIFO_MAX_FRAMES; i++) {
            if (frame_fifo.frames[i].buffer) {
                free(frame_fifo.frames[i].buffer);
                frame_fifo.frames[i].buffer = nullptr;
            }
        }
        frame_fifo.head = 0;
        frame_fifo.tail = 0;
        frame_fifo.count = 0;
        frame_fifo.not_empty.notify_all();
        frame_fifo.not_full.notify_all();
    }

    return true;
}

int RtspServer::onSessionPlay(void **data, size_t *size, uint64_t *timestamp)
{
    RtspServer *server = RtspServer::getInstance().get();
    if (server) {
        server->streamingEnabled_ = true;
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
    stream_ = video_->createVideoStream();
    if (!stream_) {
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
    cfg.fps_num = 15;
    cfg.fps_den = 1;
    cfg.rc_mode = hal::VideoRcMode::CBR;
    cfg.enable_ivdc = true;
    if (!stream_->configure(cfg)) {
        Logger::log(LogLevel::ERROR, "initialize: stream configure failed");
        return false;
    }
    return true;
}

bool RtspServer::uninitVideo(void)
{
    if (initialized) {
        if (stream_) {
            stream_->stop();
        }
        if (video_) {
            video_->exit();
        }
    }
    return true;
}

bool RtspServer::initAudio()
{
    AudioParams ap;
    ap.setDeviceType(AudioDeviceType::AUDIO_IN);
    ap.setDeviceId(1);  
    ap.setChannelId(0);
    ap.setVolume(80);    
    ap.setGain(28);      
    ap.setCodecFormat(AudioCodecFormat::G711U);
    ap.setSampleRate(AudioSampleRate::SR_16000);
    ap.setSoundMode(AudioSoundMode::MONO);
    audioSampleRate_ = ap.getSampleRateValue();
    audioNumPerFrame_ = ap.getNumPerFrame();
    audioRecorder_ = std::make_shared<AudioRecorder>();
    audioRecorder_->setAudioParams(ap);
    audioRecorder_->setAudioDataCallback(RtspServer::staticAudioDataCallback, this);
    audioRecording_ = false;
    return true;
}

bool RtspServer::uninitAudio()
{
    if (audioRecorder_) {
        audioRecorder_->stop();
        audioRecorder_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(audioDataMutex_);
        while (!audioDataQueue_.empty()) {
            audioDataQueue_.pop();
        }
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
