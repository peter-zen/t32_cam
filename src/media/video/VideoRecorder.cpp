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
#include "Common.h"
#include "Logger.h"
#include "minimp4.h"
#include "AudioRecorder.h"
#include "VideoRecorder.h"
#include "DayNightSwitch.h"
#include "MetadataDao.h"


using namespace media;

void VideoRecorder::staticAudioDataCallback(const uint8_t* data, size_t size, uint64_t timestamp, bool isKeyFrame, void* userData)
{
    VideoRecorder* recorder = static_cast<VideoRecorder*>(userData);
    if (recorder && size > 0) {
        QueuedAudioSample sample;
        sample.data.assign(data, data + size);
        sample.timestamp = timestamp;
        {
            std::lock_guard<std::mutex> lock(recorder->audioDataMutex);
            recorder->audioDataQueue.push(sample);
        }
        recorder->audioDataCond.notify_one();
    }
}

static int writeCallback(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE*)token;
    fseek(f, offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}

VideoRecorder::VideoRecorder()
    : stopRecording(false)
    , vidParam(nullptr)
    , audParam(nullptr)
    , audio_track_id(-1)
    , audioRecording(false)
    , audioThreadId(0)
    , audioRecorder(nullptr)
    , audioTimestamp(0)
    , audioSampleRate(8000)
    , audioChannels(1)
    , audioIsAac(false)
    , audioDsiSet(false)
    , lastVideoTimestamp(0)
{
    initialized = initialize();
}

VideoRecorder::VideoRecorder(const std::shared_ptr<VideoParams> vidParam, const std::shared_ptr<AudioParams> audParam)
    : vidParam(vidParam)
    , audParam(audParam)
    , audio_track_id(-1)
    , audioRecording(false)
    , audioThreadId(0)
    , audioRecorder(nullptr)
    , audioTimestamp(0)
    , audioSampleRate(8000)
    , audioChannels(1)
    , audioIsAac(false)
    , audioDsiSet(false)
    , lastVideoTimestamp(0)
{
	initialized = initialize();
}

VideoRecorder::~VideoRecorder()
{
    deinitialize();
}

bool VideoRecorder::initialize()
{
    if (!initVideo()) {
        Logger::log(LogLevel::DEBUG, "Video init error");
        uninitVideo();
        return false;
    }
    Logger::log(LogLevel::DEBUG, "Video init success");

    if (audParam) {
        if (!initAudio()) {
            Logger::log(LogLevel::ERROR, "Audio init failed");
            uninitVideo();
            return false;
        }
        Logger::log(LogLevel::DEBUG, "Audio init success");
    }
    return true;
}

void VideoRecorder::deinitialize()
{
    if (initialized) {
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads.clear();

        if (audParam) {
            if (!uninitAudio()) {
                Logger::log(LogLevel::ERROR, "Audio uninit failed");
            }
        }

        if (!uninitVideo()) {
            Logger::log(LogLevel::ERROR, "Video uninit failed");
        }
    }
}

bool VideoRecorder::record(const std::string &filename, int duration)
{
    return record(filename, nullptr, duration);
}

bool VideoRecorder::record(const std::string &filename, std::function<void(bool)> onRecordDone, int duration)
{
    if (!initialized) {
        Logger::log(LogLevel::ERROR, "VideoRecorder is not initialized");
        if (onRecordDone) {
            onRecordDone(false);
        }
        return false;
    }
    if (!stream_) {
        Logger::log(LogLevel::ERROR, "snap: stream_ is null");
        if (onRecordDone) onRecordDone(false);
        return false;
    }
    hal::VideoStreamInfo preInfo{};
    if (stream_) {
        if (!stream_->getInfo(preInfo)) {
            Logger::log(LogLevel::WARNING, "snap: pre-start info query failed");
        }
    }
    
    daynight_switch(true);

    if (!stream_->start()) {
        Logger::log(LogLevel::ERROR, "snap: stream start failed");
        if (onRecordDone) onRecordDone(false);
        return false;
    }

    /* Step.6 Get stream */
    bool result = true;

    if (onRecordDone) {
        this->threads.emplace_back([this, duration, filename, onRecordDone]() {
            bool nonBlockingResult = true;

            if (!this->record(VideoCodecFormat::H264, filename, duration)) {
                nonBlockingResult = false;
            }

            if (onRecordDone) {
                onRecordDone(nonBlockingResult);
            }
            stream_->stop();
        });
        return true;
    } else {
        if (!record(VideoCodecFormat::H264, filename, duration)) {
            result = false;
        }

            if (!stream_->stop()) {
            Logger::log(LogLevel::ERROR, "FrameSource StreamOff failed");
            result = false;
        }

        if (onRecordDone) {
            onRecordDone(result);
        }
        return result;
    }
}

bool VideoRecorder::record(VideoCodecFormat payloadType, const std::string &filename, int duration)
{
    int i = 0;
    int ret = 0;

    hal::VideoStreamInfo info{};
    if (stream_) {
        if (stream_->getInfo(info)) {
            
        } else {
            Logger::log(LogLevel::WARNING, "stream info: query failed");
        }
    }

    Logger::log(LogLevel::DEBUG, "%s: Open file %s", __func__, filename.c_str());

    FILE *fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        Logger::log(LogLevel::ERROR, "fopen %s failed", filename.c_str());
        return false;
    }
    MP4E_mux_t *muxer = MP4E_open(0, 0, fp, writeCallback);

    if (!muxer) {
        Logger::log(LogLevel::ERROR, "MP4E_open %s failed", filename.c_str());
        fclose(fp);
        return false;
    }

    mp4_h26x_writer_t mp4wr;
    if (MP4E_STATUS_OK != mp4_h26x_write_init(&mp4wr, muxer, info.width, info.height, payloadType == VideoCodecFormat::H265)) {
        Logger::log(LogLevel::ERROR, "mp4_h26x_write_init %s failed", filename.c_str());
        MP4E_close(muxer);
        fclose(fp);
        return false;
    }

    audio_track_id = -1;

	//int fps = params.getFps();
	int fps = info.fps_num / info.fps_den;
	int frameCount = duration*fps;
	Logger::log(LogLevel::INFO, "%s: duration:%d, frameCount:%d, fps:%d", __func__, duration, frameCount, fps);
	auto checkRecordCondition = [&frameCount, duration, this]() {
		if (this->stopRecording) {
			return false;
		}

		if (duration == 0) {
			return true;
		}

        if (frameCount > 0) {
            frameCount--;
            return true;
        }

		return false;
	};
    
    // Reset last timestamp for new recording
    lastVideoTimestamp = 0;
    
    while (checkRecordCondition()) {
    	/* Polling stream, set timeout as 1000msec */
        if (!stream_->polling(1000)) {
            Logger::log(LogLevel::ERROR, "stream_->polling(1000) timeout");
            MP4E_close(muxer);
            Logger::log(LogLevel::WARNING, "closing mp4 writer on polling timeout");
            mp4_h26x_write_close(&mp4wr);
        	fclose(fp);
            return false;
        }

        hal::VideoEncodedFrame frame;
        if (!stream_->getFrame(frame)) {
            Logger::log(LogLevel::ERROR, "getFrame failed");
            MP4E_close(muxer);
            mp4_h26x_write_close(&mp4wr);
        	fclose(fp);
            return false;
        }
        for (i = 0; i < frame.piece_count; i++) {
            // 处理视频数据
            size_t datasize = frame.pieces[i].size;
            uint8_t *inputData = (uint8_t*)frame.pieces[i].data;
            size_t pos = 0;
            
            // Calculate duration using timestamp
            int nal_duration = 90000 / fps; // default
            int64_t currentTimestamp = frame.pts;
            if (lastVideoTimestamp > 0 && currentTimestamp > lastVideoTimestamp) {
                // Convert us to 90kHz ticks: diff_us * 90000 / 1000000 = diff_us * 9 / 100
                nal_duration = (int)((currentTimestamp - lastVideoTimestamp) * 9 / 100);
            }
            if (i == 0) { // Update timestamp only once per frame (assuming all packs in frame have same timestamp or we track frame boundaries)
                lastVideoTimestamp = currentTimestamp;
            }

            while (pos < datasize) {
                ssize_t nal_size = getNALSize(inputData + pos, datasize - pos);
                if (nal_size < 4) {
                    pos += 1;
                    continue;
                }
                if (MP4E_STATUS_OK != mp4_h26x_write_nal(&mp4wr, inputData + pos, (int)nal_size, nal_duration)) {
                    stream_->releaseFrame(frame);
                    stream_->stop();
                    MP4E_close(muxer);
                    mp4_h26x_write_close(&mp4wr);
                    fclose(fp);
                    return false;
                }
                pos += nal_size;
            }
        }

        if (audioRecording) {
            QueuedAudioSample audioSample;
            bool hasAudioData = false;
            {
                std::lock_guard<std::mutex> lock(audioDataMutex);
                if (!audioDataQueue.empty()) {
                    audioSample = audioDataQueue.front();
                    audioDataQueue.pop();
                    hasAudioData = true;
                }
            }
            if (hasAudioData) {
                const std::vector<uint8_t>& audioData = audioSample.data;
                if (audioIsAac) {
                    size_t offset = 0;
                    static bool audioAdtsLogged = false;
                    while (offset + 7 <= audioData.size()) {
                        const uint8_t* p = audioData.data() + offset;
                        if (!(p[0] == 0xFF && (p[1] & 0xF0) == 0xF0)) {
                            break;
                        }
                        int protection_absent = p[1] & 0x01;
                        int headerLen = protection_absent ? 7 : 9;
                        if (offset + headerLen >= audioData.size()) {
                            break;
                        }
                        int frameLen = ((p[3] & 0x03) << 11) | (p[4] << 3) | ((p[5] & 0xE0) >> 5);
                        if (frameLen <= headerLen || offset + frameLen > audioData.size()) {
                            break;
                        }
                        if (!audioAdtsLogged) {
                            uint8_t profile = (p[2] & 0xC0) >> 6;
                            uint8_t samplingFreqIndex = (p[2] & 0x3C) >> 2;
                            uint8_t channelConfig = ((p[2] & 0x01) << 2) | ((p[3] & 0xC0) >> 6);
                            int sr = 16000;
                            switch (samplingFreqIndex) {
                                case 0: sr = 96000; break;
                                case 1: sr = 88200; break;
                                case 2: sr = 64000; break;
                                case 3: sr = 48000; break;
                                case 4: sr = 44100; break;
                                case 5: sr = 32000; break;
                                case 6: sr = 24000; break;
                                case 7: sr = 22050; break;
                                case 8: sr = 16000; break;
                                case 9: sr = 12000; break;
                                case 10: sr = 11025; break;
                                case 11: sr = 8000; break;
                                default: sr = 16000; break;
                            }
                            Logger::log(LogLevel::INFO, "ADTS header: profile=%d sfi=%d sr=%d ch=%d frameLen=%d", profile, samplingFreqIndex, sr, channelConfig, frameLen);
                            audioSampleRate = sr;
                            audioChannels = channelConfig;
                            audioAdtsLogged = true;
                        }
                        if (audio_track_id < 0) {
                            MP4E_track_t audioTrack;
                            memset(&audioTrack, 0, sizeof(MP4E_track_t));
                            audioTrack.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
                            strcpy((char*)audioTrack.language, "und");
                            audioTrack.track_media_kind = e_audio;
                            audioTrack.time_scale = 90000;
                            int sampleTicks = (audioSampleRate > 0) ? (1024 * 90000 / audioSampleRate) : (1024 * 90000 / 16000);
                            audioTrack.default_duration = sampleTicks;
                            audioTrack.u.a.channelcount = audioChannels;
                            Logger::log(LogLevel::INFO, "Add audio track: scale=%d, default_duration=%d, isAac=%d", audioTrack.time_scale, audioTrack.default_duration, audioIsAac);
                            audio_track_id = MP4E_add_track(muxer, &audioTrack);
                            if (audio_track_id < 0) {
                                Logger::log(LogLevel::ERROR, "Add audio track failed: %d", audio_track_id);
                                break;
                            } else {
                                Logger::log(LogLevel::INFO, "Add audio track success, track_id: %d", audio_track_id);
                            }
                            int sfi = 8;
                            switch (audioSampleRate) {
                                case 96000: sfi = 0; break;
                                case 88200: sfi = 1; break;
                                case 64000: sfi = 2; break;
                                case 48000: sfi = 3; break;
                                case 44100: sfi = 4; break;
                                case 32000: sfi = 5; break;
                                case 24000: sfi = 6; break;
                                case 22050: sfi = 7; break;
                                case 16000: sfi = 8; break;
                                case 12000: sfi = 9; break;
                                case 11025: sfi = 10; break;
                                case 8000:  sfi = 11; break;
                                default:    sfi = 8; break;
                            }
                            uint8_t audioObjectType = 2;
                            uint8_t chCfg = static_cast<uint8_t>(audioChannels);
                            uint8_t asc[2];
                            asc[0] = static_cast<uint8_t>((audioObjectType << 3) | (sfi >> 1));
                            asc[1] = static_cast<uint8_t>(((sfi & 0x01) << 7) | (chCfg << 3));
                            Logger::log(LogLevel::INFO, "Set AAC DSI: objectType=%d sfi=%d sampleRate=%d channels=%d", audioObjectType, sfi, audioSampleRate, audioChannels);
                            if (MP4E_STATUS_OK != MP4E_set_dsi(muxer, audio_track_id, asc, 2)) {
                                Logger::log(LogLevel::ERROR, "Set audio DSI failed");
                            } else {
                                audioDsiSet = true;
                            }
                        }
                        const uint8_t* framePayload = p + headerLen;
                        int payloadLen = frameLen - headerLen;
                        int sampleTicks = (audioSampleRate > 0) ? (1024 * 90000 / audioSampleRate) : (1024 * 90000 / 16000);
                        if (MP4E_STATUS_OK != MP4E_put_sample(muxer, audio_track_id, framePayload, payloadLen, sampleTicks, MP4E_SAMPLE_DEFAULT)) {
                            Logger::log(LogLevel::ERROR, "Write AAC audio sample failed");
                        }
                        offset += frameLen;
                    }
                } else {
                    if (audio_track_id < 0) {
                        MP4E_track_t audioTrack;
                        memset(&audioTrack, 0, sizeof(MP4E_track_t));
                        audioTrack.object_type_indication = MP4_OBJECT_TYPE_USER_PRIVATE;
                        strcpy((char*)audioTrack.language, "und");
                        audioTrack.track_media_kind = e_audio;
                        audioTrack.time_scale = 90000;
                        int sampleTicks = (audioSampleRate > 0) ? (90000 / 100) : (90000 / 100);
                        audioTrack.default_duration = sampleTicks;
                        audioTrack.u.a.channelcount = audioChannels;
                        Logger::log(LogLevel::INFO, "Add audio track: scale=%d, default_duration=%d, isAac=%d", audioTrack.time_scale, audioTrack.default_duration, audioIsAac);
                        audio_track_id = MP4E_add_track(muxer, &audioTrack);
                        if (audio_track_id < 0) {
                            Logger::log(LogLevel::ERROR, "Add audio track failed: %d", audio_track_id);
                        } else {
                            Logger::log(LogLevel::INFO, "Add audio track success, track_id: %d", audio_track_id);
                        }
                    }
                    int audioFrameDuration = 90000 / 100;
                    if (MP4E_STATUS_OK != MP4E_put_sample(muxer, audio_track_id, audioData.data(), static_cast<int>(audioData.size()), audioFrameDuration, MP4E_SAMPLE_DEFAULT)) {
                        Logger::log(LogLevel::ERROR, "Write audio sample failed");
                    }
                }
            }
        }
        
        stream_->releaseFrame(frame);
    }

    MP4E_close(muxer);
	mp4_h26x_write_close(&mp4wr);
    long fileSize = ftell(fp);
	fclose(fp);

    if (!stream_->stop()) {
        Logger::log(LogLevel::ERROR, "stop stream failed");
        return false;
    }

    audio_track_id = -1;
    {
        std::lock_guard<std::mutex> lock(audioDataMutex);
        while (!audioDataQueue.empty()) {
            audioDataQueue.pop();
        }
    }
    audioTimestamp = 0;
    audioDsiSet = false;
    
    // Save to DB
    MediaItem item;
    item.filePath = filename;
    item.type = 2; // Video
    item.timestamp = time(NULL); // Current time
    item.fileSize = fileSize;
    item.duration = duration;
    item.width = info.width;
    item.height = info.height;
    
    MetadataDao dao;
    if (dao.addMedia(item)) {
        Logger::log(LogLevel::INFO, "Saved video to DB: %s", filename.c_str());
    } else {
        Logger::log(LogLevel::ERROR, "Failed to save video to DB: %s", filename.c_str());
    }
    
    return true;
}

bool VideoRecorder::initVideo()
{
    video_ = hal::HalFactory::createVideo();
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
    int w = 0, h = 0;
    vidParam->getResolution(w, h);
    hal::VideoStreamConfig cfg;
    memset(&cfg, 0, sizeof(hal::VideoStreamConfig));
    cfg.payload = hal::VideoPayloadType::H264;
    cfg.channel.sensor_index = VIDEO_SENSOR_ID;
    cfg.channel.stream_index = VIDEO_STREAM_ID;
    cfg.width = w;
    cfg.height = h;
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

bool VideoRecorder::uninitVideo(void)
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

ssize_t VideoRecorder::getNALSize(uint8_t *buf, ssize_t size)
{
    ssize_t pos = 3;
    while ((size - pos) > 3)
    {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)
            return pos;
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1)
            return pos;
        pos++;
    }
    return size;
}

bool VideoRecorder::stopRecorder()
{
    stopRecording = true;
    return true;
}

bool VideoRecorder::daynight_switch(bool on)
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

bool VideoRecorder::initAudio()
{
    if (!audParam) {
        Logger::log(LogLevel::ERROR, "Audio params is null");
        return false;
    }

    try {
        AudioDeviceType deviceType = audParam->getDeviceType();
        AudioCodecFormat codecFormat = audParam->getCodecFormat();
        audioIsAac = (codecFormat == AudioCodecFormat::AAC);
        audioDsiSet = false;
        audioRecorder = std::make_shared<AudioRecorder>();
        audioRecorder->setAudioParams(*audParam);
        audioRecorder->setAudioDataCallback(staticAudioDataCallback, this);

        if (!audioRecorder->start()) {
            Logger::log(LogLevel::ERROR, "Start audio recorder failed");
            audioRecorder.reset();
            return false;
        }

        int sampleRate = audParam->getSampleRateValue();
        int channels = audParam->getChannelCount();
        int bitDepth = audParam->getBitWidthValue();
        
        audioSampleRate = sampleRate;
        audioChannels = channels;
        audioTimestamp = 0;

        audioRecording = true;
        Logger::log(LogLevel::INFO, "Audio recorder init success: sampleRate=%d, channels=%d, bitDepth=%d", 
                   audioSampleRate, audioChannels, bitDepth);
        return true;
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "Init audio failed: %s", e.what());
        audioRecorder.reset();
        return false;
    }
}

bool VideoRecorder::uninitAudio()
{
    if (audioRecorder) {
        try {
            audioRecorder->stop();
            audioRecorder.reset();
            audioRecording = false;
            
            // 清空音频数据队列
            {
                std::lock_guard<std::mutex> lock(audioDataMutex);
                while (!audioDataQueue.empty()) {
                    audioDataQueue.pop();
                }
            }
            
            Logger::log(LogLevel::INFO, "Audio recorder uninit success");
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERROR, "Uninit audio failed: %s", e.what());
            return false;
        }
    }
    return true;
}
