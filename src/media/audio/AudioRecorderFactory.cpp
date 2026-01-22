#include "AudioRecorderFactory.h"
#include "AudioIn.h"
#include "DmicIn.h"
#include <stdio.h>
#include "Logger.h"

using namespace media;

bool AudioRecorderFactory::driverLoaded[static_cast<int>(AudioDeviceType::MAX)];

IAudioRecorder* AudioRecorderFactory::createRecorder(const AudioParams& params) {
    IAudioRecorder* recorder = nullptr;
    AudioDeviceType type = params.getDeviceType();
    
    if (!driverLoaded[static_cast<int>(type)] && !loadDriver(type)) {
        Logger::log(LogLevel::ERROR, "[AudioRecorderFactory] Failed to load AudioIn driver\n");
        return recorder;
    }

    switch (type) {
        case AudioDeviceType::AUDIO_IN:
            recorder = new AudioIn();
            break;
        case AudioDeviceType::DMIC_IN:
            recorder = new DmicIn();
            break;
        default:
            Logger::log(LogLevel::ERROR, "[AudioRecorderFactory] Unsupported recorder type\n");
            break;
    }
    
    return recorder;
}

void AudioRecorderFactory::destroyRecorder(IAudioRecorder* recorder) {
    if (recorder) {
        delete recorder;
        recorder = nullptr;
    }
}

bool AudioRecorderFactory::loadDriver(AudioDeviceType type) {
    bool success = false;
    switch (type) {
        case AudioDeviceType::AUDIO_IN:
            success = AudioIn::loadDriver();
            break;
        case AudioDeviceType::DMIC_IN:
            success = DmicIn::loadDriver();
            break;
        default:
            Logger::log(LogLevel::ERROR, "[AudioRecorderFactory] Unsupported recorder type\n");
            break;
    }
    if (success) {
        driverLoaded[static_cast<int>(type)] = true;
    }
    return success;
}
