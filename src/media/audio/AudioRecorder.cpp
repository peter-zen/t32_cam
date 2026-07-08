#include "AudioRecorder.h"
#include "IAudio.h"
#include "HalProvider.h"
#include "Logger.h"
#include "Misc.h"
#include <stdint.h>
#include <string.h>
#include <chrono>
#include <vector>

using namespace media;

AudioRecorder::AudioRecorder()
    : isRecording_(false),
      recordFilePath(""),
      recordFile(nullptr),
      recordThread(nullptr),
      timerThread(nullptr),
      threadRunning(false),
      audioDataCallback(nullptr),
      audioDataCallbackUserData(nullptr),
      currentTimestamp(0),
      audio_(nullptr),
      stream_(nullptr) {
    audioParams.setDeviceId(0);
    audioParams.setChannelId(0);
}

AudioRecorder::~AudioRecorder() {
    stop();
}

void AudioRecorder::setAudioParams(const AudioParams& params) {
    audioParams = params;
}

void AudioRecorder::setRecordFilePath(const std::string& filePath) {
    recordFilePath = filePath;
}

void AudioRecorder::setAudioDataCallback(AudioDataCallback callback, void* userData) {
    audioDataCallback = callback;
    audioDataCallbackUserData = userData;
}

uint64_t AudioRecorder::getCurrentTimestamp() const {
    return currentTimestamp;
}

bool AudioRecorder::loadDriver(AudioDeviceType type) {
#if !BUILD_FOR_SIMULATION
    if (Misc::moduleLoaded("audio")) {
        return true;
    }
    std::string command = "insmod /system/modules/audio/audio.ko";
    if (type == AudioDeviceType::DMIC_IN) {
        command += " dmic_enable=1 dmic_gpio=1";
    }
    command += " spk_gpio=-1 spk_level=-1";
    int ret = Misc::syscall(command.c_str());
    if (ret != 0) {
        return true;
    }
#endif
    return true;
}

bool AudioRecorder::start() {
    if (isRecording_) {
        Logger::log(LogLevel::WARNING, "[AudioRecorder] Already recording");
        return false;
    }
    if (!loadDriver(audioParams.getDeviceType())) {
        Logger::log(LogLevel::ERROR, "[AudioRecorder] Load driver failed");
        return false;
    }
    if (!recordFilePath.empty()) {
        recordFile = fopen(recordFilePath.c_str(), "wb");
        if (!recordFile) {
            Logger::log(LogLevel::ERROR, "[AudioRecorder] Open record file failed: %s", recordFilePath.c_str());
        }
    } else {
        recordFile = nullptr;
    }
    audio_ = hal::HalProvider::createAudio();
    if (!audio_) {
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    audio_->init();
    stream_ = audio_->createAudioStream();
    if (!stream_) {
        audio_->exit();
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    hal::AudioStreamConfig cfg;
    AudioCodecFormat codec = audioParams.getCodecFormat();
    switch (codec) {
        case AudioCodecFormat::G711A: cfg.payload = hal::AudioPayloadType::G711A; break;
        case AudioCodecFormat::G711U: cfg.payload = hal::AudioPayloadType::G711U; break;
        case AudioCodecFormat::AAC: cfg.payload = hal::AudioPayloadType::AAC; break;
        default: cfg.payload = hal::AudioPayloadType::PCM16; break;
    }
    cfg.channel.device_index = audioParams.getDeviceId();
    cfg.channel.channel_index = audioParams.getChannelId();
    cfg.sample_rate = audioParams.getSampleRateValue();
    cfg.channels = (audioParams.getSoundMode() == AudioSoundMode::MONO) ? 1 : 2;
    cfg.bit_width = audioParams.getBitWidthValue();
    cfg.num_per_frame = audioParams.getNumPerFrame();
    cfg.frame_num = audioParams.getFrameNum();
    cfg.volume = audioParams.getVolume();
    cfg.gain = audioParams.getGain();
    cfg.bitrate_per_channel = audioParams.getAacBitRatePerChannel();
    AacQualityProfile ap = audioParams.getAacQualityProfile();
    cfg.quality = (ap == AacQualityProfile::ENVIRONMENT) ? 1 : ((ap == AacQualityProfile::MUSIC_HIGH) ? 2 : 0);
    cfg.input = (audioParams.getDeviceType() == AudioDeviceType::DMIC_IN) ? hal::AudioInputType::DMIC : hal::AudioInputType::AI;
    if (!stream_->configure(cfg)) {
        audio_->exit();
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        return false;
    }
    stream_->start();
    threadRunning = true;
    try {
        recordThread = std::make_shared<std::thread>(&AudioRecorder::recordThreadFunc, this);
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "[AudioRecorder] Create record thread failed: %s", e.what());
        if (stream_) { stream_->stop(); stream_.reset(); }
        if (audio_) { audio_->exit(); audio_.reset(); }
        if (recordFile) {
            fclose(recordFile);
            recordFile = nullptr;
        }
        threadRunning = false;
        return false;
    }
    isRecording_ = true;
    return true;
}

bool AudioRecorder::stop() {
    if (!isRecording_) {
        return false;
    }
    isRecording_ = false;
    if (timerThread) {
        if (timerThread->get_id() == std::this_thread::get_id()) {
            timerThread->detach();
        } else {
            timerThread->join();
        }
        timerThread.reset();
    }
    if (recordThread) {
        threadRunning = false;
        recordThread->join();
        recordThread.reset();
    }
    if (stream_) { stream_->stop(); stream_.reset(); }
    if (audio_) { audio_->exit(); audio_.reset(); }
    if (recordFile) {
        fclose(recordFile);
        recordFile = nullptr;
    }
    return true;
}

bool AudioRecorder::isRecording() const {
    return isRecording_;
}

bool AudioRecorder::recordFor(int durationMs) {
    if (isRecording_) {
        return false;
    }
    if (!start()) {
        return false;
    }
    try {
        timerThread = std::make_shared<std::thread>([this, durationMs]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
            stop();
        });
    } catch (...) {
        stop();
        return false;
    }
    return true;
}

void AudioRecorder::recordThreadFunc() {
    while (threadRunning) {
        if (!stream_) break;
        int sr = audioParams.getSampleRateValue();
        int npf = audioParams.getNumPerFrame();
        int wait_ms = (sr > 0) ? (npf * 1000 / sr) : 40;
        if (wait_ms < 1) wait_ms = 1;
        bool polled = stream_->polling(wait_ms);
        if (!polled) continue;
        hal::AudioEncodedFrame out;
        memset(&out, 0, sizeof(out));
        bool ok = stream_->getFrame(out);
        if (!ok) continue;
        currentTimestamp = out.pts / 1000;
        for (int i = 0; i < out.piece_count; ++i) {
            const hal::AudioEncodedPiece& p = out.pieces[i];
            if (recordFile && p.size > 0) {
                fwrite(p.data, 1, p.size, recordFile);
            }
            if (audioDataCallback && p.size > 0) {
                audioDataCallback(
                    reinterpret_cast<const uint8_t*>(p.data),
                    p.size,
                    currentTimestamp,
                    out.key,
                    audioDataCallbackUserData
                );
            }
        }
        stream_->releaseFrame(out);
    }
}
