#include <vector>
#include <thread>
#include <string.h>
#include <mutex>
#include <fstream>
#include <limits>
#include <chrono>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <cstdlib>
#include "Common.h"
#include "Logger.h"
#include "minimp4.h"
#include "VideoRecorder.h"
#include "DayNightSwitch.h"
#include "MetadataDao.h"
#include "Misc.h"

using namespace media;

namespace {

const char* codecName(VideoCodecFormat format) {
    return format == VideoCodecFormat::H265 ? "H265" : "H264";
}

const char* rcModeName(VideoRcMode mode) {
    switch (mode) {
        case VideoRcMode::VBR: return "VBR";
        case VideoRcMode::CVBR: return "CVBR";
        case VideoRcMode::AVBR: return "AVBR";
        case VideoRcMode::SMART: return "SMART";
        case VideoRcMode::FIXQP: return "FIXQP";
        case VideoRcMode::CBR:
        default:
            return "CBR";
    }
}

void logStreamInfo(const char* label, const hal::VideoStreamInfo& info) {
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

void VideoRecorder::audioCaptureLoop()
{
    if (!audioStream_) return;
    int sr = audParam ? audParam->getSampleRateValue() : 16000;
    int npf = audParam ? audParam->getNumPerFrame() : 320;
    int wait_ms = (sr > 0) ? (npf * 1000 / sr) : 20;
    if (wait_ms < 1) wait_ms = 1;
    audioThreadRunning = true;
    while (audioThreadRunning) {
        bool polled = audioStream_->polling(wait_ms);
        if (!polled) continue;
        hal::AudioEncodedFrame out;
        memset(&out, 0, sizeof(out));
        bool ok = audioStream_->getFrame(out);
        if (!ok) continue;
        uint64_t ts_ms = out.pts / 1000;
        audioCurrentTimestamp = ts_ms;
        for (int i = 0; i < out.piece_count; ++i) {
            const hal::AudioEncodedPiece& p = out.pieces[i];
            if (p.size > 0) {
                QueuedAudioSample sample;
                sample.data.assign(reinterpret_cast<const uint8_t*>(p.data),
                                   reinterpret_cast<const uint8_t*>(p.data) + p.size);
                sample.timestamp = ts_ms;
                {
                    std::lock_guard<std::mutex> lock(audioDataMutex);
                    audioDataQueue.push(sample);
                }
                audioDataCond.notify_one();
            }
        }
        audioStream_->releaseFrame(out);
    }
}

static int writeCallback(int64_t offset, const void *buffer, size_t size, void *token)
{
    FILE *f = (FILE*)token;
    fseek(f, offset, SEEK_SET);
    return fwrite(buffer, 1, size, f) != size;
}

static int getAacSamplingFrequencyIndex(int sampleRate)
{
    switch (sampleRate) {
        case 96000: return 0;
        case 88200: return 1;
        case 64000: return 2;
        case 48000: return 3;
        case 44100: return 4;
        case 32000: return 5;
        case 24000: return 6;
        case 22050: return 7;
        case 16000: return 8;
        case 12000: return 9;
        case 11025: return 10;
        case 8000:  return 11;
        default:    return -1;
    }
}

static bool buildAacAudioSpecificConfig(int sampleRate, int channels, uint8_t asc[2])
{
    const int sfi = getAacSamplingFrequencyIndex(sampleRate);
    if (sfi < 0 || channels <= 0 || channels > 7) {
        return false;
    }

    const uint8_t audioObjectType = 2; // AAC LC
    const uint8_t chCfg = static_cast<uint8_t>(channels);
    asc[0] = static_cast<uint8_t>((audioObjectType << 3) | (sfi >> 1));
    asc[1] = static_cast<uint8_t>(((sfi & 0x01) << 7) | (chCfg << 3));
    return true;
}

static bool parseAacAdtsConfig(const std::vector<uint8_t>& data, int& sampleRate, int& channels)
{
    static const int sampleRates[16] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
        16000, 12000, 11025, 8000, 7350, 0, 0, 0
    };

    for (size_t offset = 0; offset + 7 <= data.size(); ++offset) {
        const uint8_t* p = data.data() + offset;
        if (!(p[0] == 0xFF && (p[1] & 0xF0) == 0xF0)) {
            continue;
        }

        const int samplingFreqIndex = (p[2] & 0x3C) >> 2;
        const int channelConfig = ((p[2] & 0x01) << 2) | ((p[3] & 0xC0) >> 6);
        if (samplingFreqIndex < 0 || samplingFreqIndex >= 16 || sampleRates[samplingFreqIndex] <= 0) {
            return false;
        }

        sampleRate = sampleRates[samplingFreqIndex];
        channels = channelConfig > 0 ? channelConfig : channels;
        return channels > 0;
    }

    return false;
}

static int addAacAudioTrack(MP4E_mux_t* muxer, int sampleRate, int channels)
{
    uint8_t asc[2] = {0, 0};
    if (!buildAacAudioSpecificConfig(sampleRate, channels, asc)) {
        return MP4E_STATUS_BAD_ARGUMENTS;
    }

    MP4E_track_t audioTrack;
    memset(&audioTrack, 0, sizeof(MP4E_track_t));
    audioTrack.object_type_indication = MP4_OBJECT_TYPE_AUDIO_ISO_IEC_14496_3;
    strcpy((char*)audioTrack.language, "und");
    audioTrack.track_media_kind = e_audio;
    audioTrack.time_scale = sampleRate;
    audioTrack.default_duration = 1024;
    audioTrack.u.a.channelcount = channels;

    const int trackId = MP4E_add_track(muxer, &audioTrack);
    if (trackId < 0) {
        return trackId;
    }
    if (MP4E_set_dsi(muxer, trackId, asc, sizeof(asc)) != MP4E_STATUS_OK) {
        return MP4E_STATUS_BAD_ARGUMENTS;
    }
    return trackId;
}

static int addPrivateAudioTrack(MP4E_mux_t* muxer, int sampleRate, int channels, int defaultDuration)
{
    MP4E_track_t audioTrack;
    memset(&audioTrack, 0, sizeof(MP4E_track_t));
    audioTrack.object_type_indication = MP4_OBJECT_TYPE_USER_PRIVATE;
    strcpy((char*)audioTrack.language, "und");
    audioTrack.track_media_kind = e_audio;
    audioTrack.time_scale = sampleRate > 0 ? sampleRate : 16000;
    audioTrack.default_duration = defaultDuration > 0 ? defaultDuration : (audioTrack.time_scale / 100);
    audioTrack.u.a.channelcount = channels > 0 ? channels : 1;
    return MP4E_add_track(muxer, &audioTrack);
}

VideoRecorder::VideoRecorder()
    : vidParam(nullptr)
    , audParam(nullptr)
    , stopRecording(false)
    , audio_track_id(-1)
    , audioRecording(false)
    , audioThreadId(0)
    , audio_(nullptr)
    , audioStream_(nullptr)
    , audioThread(nullptr)
    , audioThreadRunning(false)
    , audioTimestamp(0)
    , audioCurrentTimestamp(0)
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
    , stopRecording(false)
    , audio_track_id(-1)
    , audioRecording(false)
    , audioThreadId(0)
    , audio_(nullptr)
    , audioStream_(nullptr)
    , audioThread(nullptr)
    , audioThreadRunning(false)
    , audioTimestamp(0)
    , audioCurrentTimestamp(0)
    , audioSampleRate(8000)
    , audioChannels(1)
    , audioIsAac(false)
    , audioDsiSet(false)
    , lastVideoTimestamp(0)
{
	initialized = initialize();
}

VideoRecorder::VideoRecorder(const std::shared_ptr<VideoParams> vidParam, const std::shared_ptr<AudioParams> audParam, bool concurrentSnap)
    : vidParam(vidParam)
    , audParam(audParam)
    , stopRecording(false)
    , audio_track_id(-1)
    , audioRecording(false)
    , audioThreadId(0)
    , audio_(nullptr)
    , audioStream_(nullptr)
    , audioThread(nullptr)
    , audioThreadRunning(false)
    , audioTimestamp(0)
    , audioCurrentTimestamp(0)
    , audioSampleRate(8000)
    , audioChannels(1)
    , audioIsAac(false)
    , audioDsiSet(false)
    , lastVideoTimestamp(0)
    , concurrentSnapEnabled_(concurrentSnap)
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
        } else {
            logStreamInfo("record stream info pre-start", preInfo);
        }
    }

    if (vidParam) {
        int reqWidth = 0;
        int reqHeight = 0;
        vidParam->getResolution(reqWidth, reqHeight);
        Logger::log(LogLevel::INFO,
                    "record config: file=%s sensor=%d stream=%d requested_size=%dx%d requested_fps=%d bitrate=%dKbps gop=%d codec=%s rc=%s duration=%d",
                    filename.c_str(),
                    VIDEO_SENSOR_ID,
                    preInfo.output_index,
                    reqWidth,
                    reqHeight,
                    vidParam->getFrameRate(),
                    vidParam->getBitrate(),
                    vidParam->getGop(),
                    codecName(vidParam->getCodecFormat()),
                    rcModeName(vidParam->getRcMode()),
                    duration);
    }
    
    daynight_switch(true);

    if (!stream_->start()) {
        Logger::log(LogLevel::ERROR, "snap: stream start failed");
        if (onRecordDone) onRecordDone(false);
        return false;
    }

    hal::VideoStreamInfo startedInfo{};
    if (stream_ && stream_->getInfo(startedInfo)) {
        logStreamInfo("record stream info started", startedInfo);
    } else {
        Logger::log(LogLevel::WARNING, "record stream info started: query failed");
    }

    /* Step.6 Get stream */
    bool result = true;

    if (onRecordDone) {
        this->threads.emplace_back([this, duration, filename, onRecordDone]() {
            bool nonBlockingResult = true;

            if (!this->record(vidParam ? vidParam->getCodecFormat() : VideoCodecFormat::H264, filename, duration)) {
                nonBlockingResult = false;
            }

            if (onRecordDone) {
                onRecordDone(nonBlockingResult);
            }
            stream_->stop();
        });
        return true;
    } else {
        if (!record(vidParam ? vidParam->getCodecFormat() : VideoCodecFormat::H264, filename, duration)) {
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

    hal::VideoStreamInfo info{};
    if (stream_) {
        if (stream_->getInfo(info)) {
            logStreamInfo("record stream info active", info);
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
    MP4E_mux_t *muxer = MP4E_open(0, 1, fp, writeCallback);

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
    int configuredAudioChannels = audioChannels > 0 ? audioChannels : 1;
    bool playbackCapable = true;
    std::string playbackReason;

    if (audioRecording) {
        if (audioIsAac) {
            int parsedSampleRate = audioSampleRate;
            int parsedChannels = configuredAudioChannels;
            bool parsedAdtsConfig = false;
            for (int waitMs = 0; waitMs <= 500 && !parsedAdtsConfig; waitMs += 10) {
                {
                    std::lock_guard<std::mutex> lock(audioDataMutex);
                    if (!audioDataQueue.empty()) {
                        parsedAdtsConfig = parseAacAdtsConfig(audioDataQueue.front().data, parsedSampleRate, parsedChannels);
                    }
                }
                if (!parsedAdtsConfig) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            if (parsedAdtsConfig) {
                audioSampleRate = parsedSampleRate;
                audioChannels = parsedChannels;
                configuredAudioChannels = parsedChannels;
            } else {
                Logger::log(LogLevel::WARNING,
                            "AAC ADTS preflight did not find a frame; using configured audio params sampleRate=%d channels=%d",
                            audioSampleRate, configuredAudioChannels);
            }
            audio_track_id = addAacAudioTrack(muxer, audioSampleRate, configuredAudioChannels);
            if (audio_track_id < 0) {
                Logger::log(LogLevel::ERROR,
                            "Failed to predeclare AAC track before first fMP4 fragment: sampleRate=%d channels=%d",
                            audioSampleRate, configuredAudioChannels);
                MP4E_close(muxer);
                mp4_h26x_write_close(&mp4wr);
                fclose(fp);
                return false;
            }
            audioDsiSet = true;
        } else {
            const int defaultDuration = audParam ? audParam->getNumPerFrame() : (audioSampleRate / 100);
            audio_track_id = addPrivateAudioTrack(muxer, audioSampleRate, configuredAudioChannels, defaultDuration);
            if (audio_track_id < 0) {
                Logger::log(LogLevel::ERROR, "Failed to predeclare private audio track before first fMP4 fragment");
                MP4E_close(muxer);
                mp4_h26x_write_close(&mp4wr);
                fclose(fp);
                return false;
            }
            playbackCapable = false;
            playbackReason = "audio_codec_not_playback_capable";
        }
    }

    int fps = info.fps_den > 0 ? (info.fps_num / info.fps_den) : 0;
    if (fps <= 0) {
        fps = vidParam ? vidParam->getFrameRate() : 30;
    }
    if (fps <= 0) {
        fps = 30;
    }
    int effectiveGopFrames = vidParam ? vidParam->getGop() : 0;
    if (effectiveGopFrames <= 0) {
        effectiveGopFrames = fps > 0 ? fps * 2 : 60;
    }
    int effectiveGopMs = (fps > 0) ? (effectiveGopFrames * 1000 / fps) : 2000;
    int frameCount = duration * fps;
    Logger::log(LogLevel::INFO, "%s: duration:%d, frameCount:%d, fps:%d", __func__, duration, frameCount, fps);

    int videoFrameCount = 0;
    int nalWriteCount = 0;
    uint64_t videoBytes = 0;
    int64_t firstVideoTimestamp = 0;
    int64_t prevVideoTimestamp = 0;
    int64_t lastObservedVideoTimestamp = 0;
    int64_t timestampDeltaSum = 0;
    int64_t timestampDeltaMin = std::numeric_limits<int64_t>::max();
    int64_t timestampDeltaMax = 0;
    auto recordWallStart = std::chrono::steady_clock::now();
    int64_t loopWallUsSum = 0;
    int64_t loopWallUsMax = 0;
    int64_t pollWallUsSum = 0;
    int64_t writeWallUsSum = 0;
    int64_t audioWallUsSum = 0;

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

    /* Capture thumbnail from CH2 at recording start (CH2 is free, no concurrent photo) */
    if (concurrentSnapEnabled_) {
        Logger::log(LogLevel::INFO, "record: capturing thumbnail from CH2...");
        if (captureThumbnail()) {
            Logger::log(LogLevel::INFO, "record: thumbnail captured %zu bytes", thumbData_.size());
        } else {
            Logger::log(LogLevel::WARNING, "record: thumbnail capture failed");
        }
    } else {
        Logger::log(LogLevel::INFO, "record: thumbnail skipped (concurrentSnap not enabled)");
    }


    // Pre-roll: wait for the first audio timestamp to align AV start
    if (audioRecording) {
        int wait_ms_total = 0;
        while (audioThread && audioThreadRunning && audioCurrentTimestamp == 0 && wait_ms_total < 500) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            wait_ms_total += 10;
        }
    }
    
    bool audioAdtsLogged = false;
    while (checkRecordCondition()) {
        auto loopWallStart = std::chrono::steady_clock::now();
        /* Polling stream, set timeout as 1000msec */
        auto pollWallStart = std::chrono::steady_clock::now();
        if (!stream_->polling(1000)) {
            Logger::log(LogLevel::ERROR, "stream_->polling(1000) timeout");
            MP4E_close(muxer);
            Logger::log(LogLevel::WARNING, "closing mp4 writer on polling timeout");
            mp4_h26x_write_close(&mp4wr);
            fclose(fp);
            return false;
        }
        pollWallUsSum += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - pollWallStart).count();

        hal::VideoEncodedFrame frame;
        if (!stream_->getFrame(frame)) {
            Logger::log(LogLevel::ERROR, "getFrame failed");
            MP4E_close(muxer);
            mp4_h26x_write_close(&mp4wr);
            fclose(fp);
            return false;
        }

        videoFrameCount++;
        if (firstVideoTimestamp == 0) {
            firstVideoTimestamp = static_cast<int64_t>(frame.pts);
        }
        if (prevVideoTimestamp > 0 && static_cast<int64_t>(frame.pts) > prevVideoTimestamp) {
            int64_t delta = static_cast<int64_t>(frame.pts) - prevVideoTimestamp;
            timestampDeltaSum += delta;
            if (delta < timestampDeltaMin) {
                timestampDeltaMin = delta;
            }
            if (delta > timestampDeltaMax) {
                timestampDeltaMax = delta;
            }
        }
        prevVideoTimestamp = static_cast<int64_t>(frame.pts);
        lastObservedVideoTimestamp = static_cast<int64_t>(frame.pts);

        auto writeWallStart = std::chrono::steady_clock::now();
        for (i = 0; i < frame.piece_count; i++) {
            // 处理视频数据
            size_t datasize = frame.pieces[i].size;
            videoBytes += datasize;
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
                nalWriteCount++;
                pos += nal_size;
            }
        }
        writeWallUsSum += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - writeWallStart).count();

        if (fps > 0 && (videoFrameCount % (fps * 5)) == 0 && lastObservedVideoTimestamp > firstVideoTimestamp) {
            double observedSec = (lastObservedVideoTimestamp - firstVideoTimestamp) / 1000000.0;
            double observedFps = observedSec > 0.0 ? ((videoFrameCount - 1) / observedSec) : 0.0;
            double avgDeltaMs = videoFrameCount > 1
                ? (timestampDeltaSum / 1000.0 / (videoFrameCount - 1))
                : 0.0;
            Logger::log(LogLevel::INFO,
                        "record stats: frames=%d nal=%d bytes=%llu observed_fps=%.2f avg_delta_ms=%.2f min_delta_ms=%.2f max_delta_ms=%.2f",
                        videoFrameCount,
                        nalWriteCount,
                        (unsigned long long)videoBytes,
                        observedFps,
                        avgDeltaMs,
                        timestampDeltaMin == std::numeric_limits<int64_t>::max() ? 0.0 : timestampDeltaMin / 1000.0,
                        timestampDeltaMax / 1000.0);
        }

        if (audioRecording) {
            auto audioWallStart = std::chrono::steady_clock::now();
            while (true) {
                QueuedAudioSample audioSample;
                bool popped = false;
                {
                    std::lock_guard<std::mutex> lock(audioDataMutex);
                    if (!audioDataQueue.empty()) {
                        audioSample = audioDataQueue.front();
                        audioDataQueue.pop();
                        popped = true;
                    }
                }
                if (!popped) break;
                audioTimestamp = audioSample.timestamp;
                const std::vector<uint8_t>& audioData = audioSample.data;
                if (audioIsAac) {
                    size_t offset = 0;
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
                            audioSampleRate = sr;
                            int parsedChannels = channelConfig;
                            if (parsedChannels == 0) {
                                parsedChannels = configuredAudioChannels;
                            }
                            audioChannels = parsedChannels > 0 ? parsedChannels : 1;
                            audioAdtsLogged = true;
                        }
                        if (audio_track_id < 0) {
                            break;
                        }
                        const uint8_t* framePayload = p + headerLen;
                        int payloadLen = frameLen - headerLen;
                        int sampleTicks = 1024;
                        MP4E_put_sample(muxer, audio_track_id, framePayload, payloadLen, sampleTicks, MP4E_SAMPLE_DEFAULT);
                        offset += frameLen;
                    }
                } else {
                    if (audio_track_id < 0) {
                        continue;
                    }
                    int bytesPerSample = audioChannels * 2;
                    int pktSamples = (int)audioData.size() / (bytesPerSample > 0 ? bytesPerSample : 2);
                    int audioFrameDuration = (pktSamples > 0) ? pktSamples : ((audioSampleRate > 0) ? (audioSampleRate / 100) : (16000 / 100));
                    MP4E_put_sample(muxer, audio_track_id, audioData.data(), static_cast<int>(audioData.size()), audioFrameDuration, MP4E_SAMPLE_DEFAULT);
                }
            }
            audioWallUsSum += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - audioWallStart).count();
        }
        
        stream_->releaseFrame(frame);

        int64_t loopWallUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - loopWallStart).count();
        loopWallUsSum += loopWallUs;
        if (loopWallUs > loopWallUsMax) {
            loopWallUsMax = loopWallUs;
        }
        if (fps > 0 && (videoFrameCount % (fps * 5)) == 0) {
            double wallSec = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - recordWallStart).count() / 1000000.0;
            double wallFps = wallSec > 0.0 ? (videoFrameCount / wallSec) : 0.0;
            double avgLoopMs = videoFrameCount > 0 ? (loopWallUsSum / 1000.0 / videoFrameCount) : 0.0;
            double avgPollMs = videoFrameCount > 0 ? (pollWallUsSum / 1000.0 / videoFrameCount) : 0.0;
            double avgWriteMs = videoFrameCount > 0 ? (writeWallUsSum / 1000.0 / videoFrameCount) : 0.0;
            double avgAudioMs = videoFrameCount > 0 ? (audioWallUsSum / 1000.0 / videoFrameCount) : 0.0;
            Logger::log(LogLevel::INFO,
                        "record wall: frames=%d wall_fps=%.2f avg_loop_ms=%.2f max_loop_ms=%.2f avg_poll_ms=%.2f avg_write_ms=%.2f avg_audio_ms=%.2f",
                        videoFrameCount,
                        wallFps,
                        avgLoopMs,
                        loopWallUsMax / 1000.0,
                        avgPollMs,
                        avgWriteMs,
                        avgAudioMs);
        }
    }

    if (audioRecording) {
        uint64_t videoEndMs = lastVideoTimestamp > 0 ? (uint64_t)(lastVideoTimestamp / 1000) : 0;
        int waitLoops = 0;
        while (audioTimestamp < videoEndMs && waitLoops < 50) {
            QueuedAudioSample audioSample;
            bool popped = false;
            {
                std::unique_lock<std::mutex> lock(audioDataMutex);
                audioDataCond.wait_for(lock, std::chrono::milliseconds(10), [&]() { return !audioDataQueue.empty(); });
                if (!audioDataQueue.empty()) {
                    audioSample = audioDataQueue.front();
                    audioDataQueue.pop();
                    popped = true;
                }
            }
            if (!popped) {
                waitLoops++;
                continue;
            }
            audioTimestamp = audioSample.timestamp;
            const std::vector<uint8_t>& audioData = audioSample.data;
            if (audioIsAac) {
                size_t offset = 0;
                while (offset + 7 <= audioData.size()) {
                    const uint8_t* p = audioData.data() + offset;
                    if (!(p[0] == 0xFF && (p[1] & 0xF0) == 0xF0)) break;
                    int protection_absent = p[1] & 0x01;
                    int headerLen = protection_absent ? 7 : 9;
                    if (offset + headerLen >= audioData.size()) break;
                    int frameLen = ((p[3] & 0x03) << 11) | (p[4] << 3) | ((p[5] & 0xE0) >> 5);
                    if (frameLen <= headerLen || offset + frameLen > audioData.size()) break;
                    const uint8_t* framePayload = p + headerLen;
                    int payloadLen = frameLen - headerLen;
                    if (audio_track_id >= 0) {
                        MP4E_put_sample(muxer, audio_track_id, framePayload, payloadLen, 1024, MP4E_SAMPLE_DEFAULT);
                    }
                    offset += frameLen;
                }
            } else {
                int bytesPerSample = audioChannels * 2;
                int pktSamples = (int)audioData.size() / (bytesPerSample > 0 ? bytesPerSample : 2);
                int audioFrameDuration = (pktSamples > 0) ? pktSamples : ((audioSampleRate > 0) ? (audioSampleRate / 100) : (16000 / 100));
                if (audio_track_id >= 0) {
                    MP4E_put_sample(muxer, audio_track_id, audioData.data(), static_cast<int>(audioData.size()), audioFrameDuration, MP4E_SAMPLE_DEFAULT);
                }
            }
        }
        while (true) {
            QueuedAudioSample audioSample;
            bool popped = false;
            {
                std::lock_guard<std::mutex> lock(audioDataMutex);
                if (!audioDataQueue.empty()) {
                    audioSample = audioDataQueue.front();
                    audioDataQueue.pop();
                    popped = true;
                }
            }
            if (!popped) break;
            const std::vector<uint8_t>& audioData = audioSample.data;
            if (audioIsAac) {
                size_t offset = 0;
                while (offset + 7 <= audioData.size()) {
                    const uint8_t* p = audioData.data() + offset;
                    if (!(p[0] == 0xFF && (p[1] & 0xF0) == 0xF0)) break;
                    int protection_absent = p[1] & 0x01;
                    int headerLen = protection_absent ? 7 : 9;
                    if (offset + headerLen >= audioData.size()) break;
                    int frameLen = ((p[3] & 0x03) << 11) | (p[4] << 3) | ((p[5] & 0xE0) >> 5);
                    if (frameLen <= headerLen || offset + frameLen > audioData.size()) break;
                    const uint8_t* framePayload = p + headerLen;
                    int payloadLen = frameLen - headerLen;
                    if (audio_track_id >= 0) {
                        MP4E_put_sample(muxer, audio_track_id, framePayload, payloadLen, 1024, MP4E_SAMPLE_DEFAULT);
                    }
                    offset += frameLen;
                }
            } else {
                int bytesPerSample = audioChannels * 2;
                int pktSamples = (int)audioData.size() / (bytesPerSample > 0 ? bytesPerSample : 2);
                int audioFrameDuration = (pktSamples > 0) ? pktSamples : ((audioSampleRate > 0) ? (audioSampleRate / 100) : (16000 / 100));
                if (audio_track_id >= 0) {
                    MP4E_put_sample(muxer, audio_track_id, audioData.data(), static_cast<int>(audioData.size()), audioFrameDuration, MP4E_SAMPLE_DEFAULT);
                }
            }
        }
    }
    if (videoFrameCount > 0) {
        double observedSec = (lastObservedVideoTimestamp > firstVideoTimestamp)
            ? ((lastObservedVideoTimestamp - firstVideoTimestamp) / 1000000.0)
            : 0.0;
        double observedFps = observedSec > 0.0 ? ((videoFrameCount - 1) / observedSec) : 0.0;
        double avgDeltaMs = videoFrameCount > 1
            ? (timestampDeltaSum / 1000.0 / (videoFrameCount - 1))
            : 0.0;
        double wallSec = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - recordWallStart).count() / 1000000.0;
        double wallFps = wallSec > 0.0 ? (videoFrameCount / wallSec) : 0.0;
        double avgLoopMs = videoFrameCount > 0 ? (loopWallUsSum / 1000.0 / videoFrameCount) : 0.0;
        double avgPollMs = videoFrameCount > 0 ? (pollWallUsSum / 1000.0 / videoFrameCount) : 0.0;
        double avgWriteMs = videoFrameCount > 0 ? (writeWallUsSum / 1000.0 / videoFrameCount) : 0.0;
        double avgAudioMs = videoFrameCount > 0 ? (audioWallUsSum / 1000.0 / videoFrameCount) : 0.0;
        Logger::log(LogLevel::INFO,
                    "record summary: frames=%d nal=%d bytes=%llu requested_fps=%d observed_fps=%.2f wall_fps=%.2f avg_delta_ms=%.2f avg_loop_ms=%.2f max_loop_ms=%.2f avg_poll_ms=%.2f avg_write_ms=%.2f avg_audio_ms=%.2f min_delta_ms=%.2f max_delta_ms=%.2f",
                    videoFrameCount,
                    nalWriteCount,
                    (unsigned long long)videoBytes,
                    fps,
                    observedFps,
                    wallFps,
                    avgDeltaMs,
                    avgLoopMs,
                    loopWallUsMax / 1000.0,
                    avgPollMs,
                    avgWriteMs,
                    avgAudioMs,
                    timestampDeltaMin == std::numeric_limits<int64_t>::max() ? 0.0 : timestampDeltaMin / 1000.0,
                    timestampDeltaMax / 1000.0);
    }
    MP4E_close(muxer);
    mp4_h26x_write_close(&mp4wr);
    fflush(fp);
    long fileSize = 0;
    if (fseek(fp, 0, SEEK_END) == 0) {
        fileSize = ftell(fp);
        if (fileSize < 0) {
            Logger::log(LogLevel::WARNING, "Failed to get file size: %s", filename.c_str());
            fileSize = 0;
        }
    } else {
        Logger::log(LogLevel::WARNING, "Failed to seek to end for file size: %s", filename.c_str());
    }
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
    item.containerType = "fmp4";
    item.playbackCapable = playbackCapable;
    item.playbackReason = playbackCapable ? "" : playbackReason;
    item.rangeSupported = playbackCapable;
    item.seekSupport = playbackCapable ? "keyframe" : "";
    item.seekGranularityMs = playbackCapable ? effectiveGopMs : 0;
    item.effectiveGopFrames = effectiveGopFrames;
    item.effectiveGopMs = effectiveGopMs;

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
    int w = 0, h = 0;
    if (vidParam) {
        vidParam->getResolution(w, h);
    } else {
        w = 1920;
        h = 1080;
    }
    hal::VideoStreamConfig cfg;
    memset(&cfg, 0, sizeof(hal::VideoStreamConfig));
    cfg.payload = (vidParam && vidParam->getCodecFormat() == media::VideoCodecFormat::H265)
        ? hal::VideoPayloadType::H265 : hal::VideoPayloadType::H264;
    cfg.channel.sensor_index = VIDEO_SENSOR_ID;
    cfg.channel.stream_index = VIDEO_STREAM_ID;
    const char* recordStreamId = std::getenv("HTC_RECORD_STREAM_ID");
    if (recordStreamId && strcmp(recordStreamId, "1") == 0) {
        cfg.channel.stream_index = 1;
    }
    cfg.width = w;
	    cfg.height = h;
	    cfg.fps_num = vidParam ? vidParam->getFrameRate() : 30;
	    cfg.fps_den = 1;
        cfg.bitrate = vidParam ? vidParam->getBitrate() : 0;
	        cfg.gop = vidParam ? vidParam->getGop() : 0;
        if (cfg.gop <= 0) {
            cfg.gop = cfg.fps_num > 0 ? cfg.fps_num * 2 : 60;
        }
	    cfg.rc_mode = static_cast<hal::VideoRcMode>(
	        vidParam ? static_cast<int>(vidParam->getRcMode()) : static_cast<int>(hal::VideoRcMode::CBR));
	    cfg.enable_ivdc = true;
    Logger::log(LogLevel::INFO,
                "initVideo: sensor=%d stream=%d size=%dx%d fps=%d/%d bitrate=%dKbps gop=%d rc=%d codec=%d",
                cfg.channel.sensor_index,
                cfg.channel.stream_index,
                cfg.width,
                cfg.height,
                cfg.fps_num,
                cfg.fps_den,
                cfg.bitrate,
                cfg.gop,
                static_cast<int>(cfg.rc_mode),
                static_cast<int>(cfg.payload));
    if (!stream_->configure(cfg)) {
        Logger::log(LogLevel::ERROR, "initialize: stream configure failed");
        return false;
    }
    return true;
}

bool VideoRecorder::initJpegStream() {
    if (jpegStream_) return true;
    if (!video_) {
        Logger::log(LogLevel::ERROR, "initJpegStream: video_ is null");
        return false;
    }

    jpegStream_ = video_->createVideoStream();
    if (!jpegStream_) {
        Logger::log(LogLevel::ERROR, "initJpegStream: createVideoStream failed");
        return false;
    }

    hal::VideoStreamConfig cfg;
    memset(&cfg, 0, sizeof(hal::VideoStreamConfig));
    cfg.payload = hal::VideoPayloadType::JPEG;
    cfg.channel.sensor_index = VIDEO_SENSOR_ID;
    cfg.channel.stream_index = 2;  // CH2: hardware scaler for thumbnail
    cfg.width = 320;               // thumbnail width
    cfg.height = 180;              // thumbnail height (16:9)
    cfg.fps_num = 1;
    cfg.fps_den = 1;
    cfg.quality = 80;
    cfg.rc_mode = hal::VideoRcMode::FIXQP;
    cfg.enable_ivdc = true;

    Logger::log(LogLevel::INFO,
                "initJpegStream: sensor=%d stream=%d size=%dx%d quality=%d",
                cfg.channel.sensor_index, cfg.channel.stream_index,
                cfg.width, cfg.height, cfg.quality);

    if (!jpegStream_->configure(cfg)) {
        Logger::log(LogLevel::ERROR, "initJpegStream: configure failed");
        jpegStream_.reset();
        return false;
    }
    return true;
}

bool VideoRecorder::captureJpeg(const std::string& filename, int quality) {
    if (!concurrentSnapEnabled_) {
        Logger::log(LogLevel::WARNING, "captureJpeg: concurrent snap not enabled");
        return false;
    }
    if (!initialized || !video_) {
        Logger::log(LogLevel::ERROR, "captureJpeg: not initialized");
        return false;
    }

    if (!initJpegStream()) {
        Logger::log(LogLevel::ERROR, "captureJpeg: initJpegStream failed");
        return false;
    }

    if (!jpegStream_->start()) {
        Logger::log(LogLevel::ERROR, "captureJpeg: jpegStream start failed");
        return false;
    }

    if (!jpegStream_->polling(1000)) {
        Logger::log(LogLevel::ERROR, "captureJpeg: polling timeout");
        jpegStream_->stop();
        return false;
    }

    hal::VideoEncodedFrame frame;
    if (!jpegStream_->getFrame(frame)) {
        Logger::log(LogLevel::ERROR, "captureJpeg: getFrame failed");
        jpegStream_->stop();
        return false;
    }

    FILE* fp = fopen(filename.c_str(), "wb");
    if (!fp) {
        Logger::log(LogLevel::ERROR, "captureJpeg: fopen %s failed", filename.c_str());
        jpegStream_->releaseFrame(frame);
        jpegStream_->stop();
        return false;
    }

    for (int i = 0; i < frame.piece_count; ++i) {
        if (frame.pieces[i].size > 0) {
            fwrite(frame.pieces[i].data, 1, frame.pieces[i].size, fp);
        }
    }
    fclose(fp);

    jpegStream_->releaseFrame(frame);
    jpegStream_->stop();

    Logger::log(LogLevel::INFO, "captureJpeg: saved %s", filename.c_str());
    return true;
}

bool VideoRecorder::captureThumbnail() {
    thumbData_.clear();
    if (!concurrentSnapEnabled_) {
        Logger::log(LogLevel::WARNING, "captureThumbnail: concurrent snap not enabled");
        return false;
    }
    if (!initialized || !video_) {
        return false;
    }

    if (!initJpegStream()) {
        Logger::log(LogLevel::WARNING, "captureThumbnail: initJpegStream failed");
        return false;
    }

    if (!jpegStream_->start()) {
        Logger::log(LogLevel::WARNING, "captureThumbnail: jpegStream start failed");
        return false;
    }

    if (!jpegStream_->polling(1000)) {
        Logger::log(LogLevel::WARNING, "captureThumbnail: polling timeout");
        jpegStream_->stop();
        return false;
    }

    hal::VideoEncodedFrame frame;
    if (!jpegStream_->getFrame(frame)) {
        Logger::log(LogLevel::WARNING, "captureThumbnail: getFrame failed");
        jpegStream_->stop();
        return false;
    }

    /* Collect all pieces into thumbData_ */
    size_t totalSize = 0;
    for (int i = 0; i < frame.piece_count; ++i) {
        totalSize += frame.pieces[i].size;
    }
    thumbData_.reserve(totalSize);
    for (int i = 0; i < frame.piece_count; ++i) {
        const auto* p = static_cast<const uint8_t*>(frame.pieces[i].data);
        thumbData_.insert(thumbData_.end(), p, p + frame.pieces[i].size);
    }

    jpegStream_->releaseFrame(frame);
    jpegStream_->stop();

    Logger::log(LogLevel::INFO, "captureThumbnail: captured %zu bytes", thumbData_.size());
    return true;
}

bool VideoRecorder::uninitVideo(void)
{
    if (initialized) {
        if (stream_) {
            stream_->stop();
        }
        if (jpegStream_) {
            jpegStream_->stop();
            jpegStream_.reset();
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

    const char* forceDay = std::getenv("HTC_FORCE_RECORD_DAY_MODE");
    if (forceDay && strcmp(forceDay, "1") == 0) {
        Logger::log(LogLevel::INFO, "daynight_switch: force DAY mode for recording FPS debug");
        daynight_controller->controlISP(DayNightState::DAY);
        daynight_controller->controlIRCut(DayNightState::DAY);
        daynight_controller->controlIRLed(DayNightState::DAY);
        daynight_controller->suspendAutoSwitch();
        return true;
    }

    if (on) {
        auto daynight_state = daynight_controller->getDayNightState();
        Logger::log(LogLevel::INFO, "daynight_switch: detected state=%d", static_cast<int>(daynight_state));
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
        #ifndef BUILD_FOR_SIMULATION
        {
            std::string checkCommand = "lsmod | grep audio";
            if (Misc::syscall(checkCommand.c_str()) != 0) {
                std::string command = "insmod /system/modules/audio/audio.ko";
                if (audParam->getDeviceType() == AudioDeviceType::DMIC_IN) {
                    command += " dmic_enable=1 dmic_gpio=1";
                }
                command += " spk_gpio=-1 spk_level=-1";
                int ret = Misc::syscall(command.c_str(), 1000);
                (void)ret;
            }
        }
        #endif
        AudioCodecFormat codecFormat = audParam->getCodecFormat();
        audioIsAac = (codecFormat == AudioCodecFormat::AAC);
        audioDsiSet = false;
        audio_ = hal::HalProvider::createAudio();
        if (!audio_) {
            return false;
        }
        if (!audio_->init()) {
            audio_.reset();
            return false;
        }
        audioStream_ = audio_->createAudioStream();
        if (!audioStream_) {
            audio_->exit();
            audio_.reset();
            return false;
        }
        hal::AudioStreamConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        switch (codecFormat) {
            case AudioCodecFormat::G711A: cfg.payload = hal::AudioPayloadType::G711A; break;
            case AudioCodecFormat::G711U: cfg.payload = hal::AudioPayloadType::G711U; break;
            case AudioCodecFormat::AAC:   cfg.payload = hal::AudioPayloadType::AAC;   break;
            default:                      cfg.payload = hal::AudioPayloadType::PCM16; break;
        }
        cfg.channel.device_index = audParam->getDeviceId();
        cfg.channel.channel_index = audParam->getChannelId();
        cfg.sample_rate = audParam->getSampleRateValue();
        cfg.channels = (audParam->getSoundMode() == AudioSoundMode::MONO) ? 1 : 2;
        cfg.bit_width = audParam->getBitWidthValue();
        cfg.num_per_frame = audParam->getNumPerFrame();
        cfg.frame_num = audParam->getFrameNum();
        cfg.volume = audParam->getVolume();
        cfg.gain = audParam->getGain();
        cfg.bitrate_per_channel = audParam->getAacBitRatePerChannel();
        AacQualityProfile ap = audParam->getAacQualityProfile();
        cfg.quality = (ap == AacQualityProfile::ENVIRONMENT) ? 1 : ((ap == AacQualityProfile::MUSIC_HIGH) ? 2 : 0);
        cfg.input = (audParam->getDeviceType() == AudioDeviceType::DMIC_IN) ? hal::AudioInputType::DMIC : hal::AudioInputType::AI;
        if (!audioStream_->configure(cfg)) {
            audioStream_.reset();
            audio_->exit();
            audio_.reset();
            return false;
        }
        if (!audioStream_->start()) {
            audioStream_.reset();
            audio_->exit();
            audio_.reset();
            return false;
        }
        audioSampleRate = cfg.sample_rate;
        audioChannels = cfg.channels;
        audioTimestamp = 0;
        audioCurrentTimestamp = 0;
        audioThreadRunning = true;
        try {
            audioThread = std::make_shared<std::thread>(&VideoRecorder::audioCaptureLoop, this);
        } catch (...) {
            audioThreadRunning = false;
            audioStream_->stop();
            audioStream_.reset();
            audio_->exit();
            audio_.reset();
            return false;
        }
        int bitDepth = audParam->getBitWidthValue();
        audioRecording = true;
        Logger::log(LogLevel::INFO, "Audio init success: sampleRate=%d, channels=%d, bitDepth=%d",
                   audioSampleRate, audioChannels, bitDepth);
        return true;
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "Init audio failed: %s", e.what());
        audioThreadRunning = false;
        if (audioThread) {
            try { audioThread->join(); } catch (...) {}
            audioThread.reset();
        }
        if (audioStream_) {
            audioStream_->stop();
            audioStream_.reset();
        }
        if (audio_) {
            audio_->exit();
            audio_.reset();
        }
        return false;
    }
}

bool VideoRecorder::uninitAudio()
{
    if (audioThread || audioStream_ || audio_) {
        try {
            audioThreadRunning = false;
            if (audioThread) {
                audioThread->join();
                audioThread.reset();
            }
            if (audioStream_) {
                audioStream_->stop();
                audioStream_.reset();
            }
            if (audio_) {
                audio_->exit();
                audio_.reset();
            }
            audioRecording = false;
            
            // 清空音频数据队列
            {
                std::lock_guard<std::mutex> lock(audioDataMutex);
                while (!audioDataQueue.empty()) {
                    audioDataQueue.pop();
                }
            }
            
            Logger::log(LogLevel::INFO, "Audio uninit success");
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERROR, "Uninit audio failed: %s", e.what());
            return false;
        }
    }
    return true;
}
