#include "AudioParams.h"

using namespace media;

AudioParams::AudioParams()
    : deviceType(AudioDeviceType::AUDIO_IN)
    , codecFormat(AudioCodecFormat::G711A)
    , sampleRate(AudioSampleRate::SR_16000)
    , bitWidth(AudioBitWidth::BW_16)
    , soundMode(AudioSoundMode::MONO)
    , volume(60)
    , gain(12)
    , deviceId(0)
    , channelId(0)
    , numPerFrame(320)
    , frameNum(10)
    , channelCount(1)
    , aecDmicId(0)
    , needAec(false)
    , usrFrmDepth(10)
    , aacBitRatePerChannel(64000)
    , aacQualityProfile(AacQualityProfile::VOICE) {
}

void AudioParams::setCodecFormat(AudioCodecFormat format) {
    this->codecFormat = format;
}

AudioCodecFormat AudioParams::getCodecFormat() const {
    return codecFormat;
}

void AudioParams::setSampleRate(AudioSampleRate rate) {
    this->sampleRate = rate;
    switch (rate) {
        case AudioSampleRate::SR_8000:
            this->numPerFrame = 160;
            break;
        case AudioSampleRate::SR_16000:
            this->numPerFrame = 320;
            break;
        case AudioSampleRate::SR_48000:
            this->numPerFrame = 960;
            break;
        default:
            this->numPerFrame = 320;
            break;
    }
}

AudioSampleRate AudioParams::getSampleRate() const {
    return sampleRate;
}

int AudioParams::getSampleRateValue() const {
    switch (sampleRate) {
        case AudioSampleRate::SR_8000:
            return 8000;
        case AudioSampleRate::SR_16000:
            return 16000;
        case AudioSampleRate::SR_48000:
            return 48000;
        default:
            return 16000;
    }
}

void AudioParams::setVolume(int volume) {
    this->volume = volume;
}

int AudioParams::getVolume() const {
    return volume;
}

void AudioParams::setGain(int gain) {
    this->gain = gain;
}

int AudioParams::getGain() const {
    return gain;
}

void AudioParams::setDmicConfig(int aecDmicId, bool needAec) {
    this->aecDmicId = aecDmicId;
    this->needAec = needAec;
}

int AudioParams::getAecDmicId() const {
    return aecDmicId;
}

bool AudioParams::isNeedAec() const {
    return needAec;
}

void AudioParams::setDeviceType(AudioDeviceType type) {
    this->deviceType = type;
}

AudioDeviceType AudioParams::getDeviceType() const {
    return deviceType;
}

void AudioParams::setDeviceId(int devId) {
    this->deviceId = devId;
}

int AudioParams::getDeviceId() const {
    return deviceId;
}

void AudioParams::setChannelId(int chnId) {
    this->channelId = chnId;
}

int AudioParams::getChannelId() const {
    return channelId;
}

int AudioParams::getNumPerFrame() const {
    return numPerFrame;
}

int AudioParams::getFrameNum() const {
    return frameNum;
}

void AudioParams::setChannelCount(int chnCnt) {
    this->channelCount = chnCnt;
}

int AudioParams::getChannelCount() const {
    return channelCount;
}

void AudioParams::setBitWidth(AudioBitWidth bitWidth) {
    this->bitWidth = bitWidth;
}

AudioBitWidth AudioParams::getBitWidth() const {
    return bitWidth;
}

int AudioParams::getBitWidthValue() const {
    switch (bitWidth) {
        case AudioBitWidth::BW_16:
            return 16;
        default:
            return 16;
    }
}

void AudioParams::setSoundMode(AudioSoundMode soundMode) {
    this->soundMode = soundMode;
}

AudioSoundMode AudioParams::getSoundMode() const {
    return soundMode;
}

int AudioParams::getSoundModeValue() const {
    switch (soundMode) {
        case AudioSoundMode::MONO:
            return 1;  // 单声道对应1（与SDK定义一致）
        case AudioSoundMode::STEREO:
            return 2;  // 立体声对应2（与SDK定义一致）
        default:
            return 1;
    }
}

void AudioParams::setAacBitRatePerChannel(unsigned long bitRate) {
    aacBitRatePerChannel = bitRate;
}

unsigned long AudioParams::getAacBitRatePerChannel() const {
    return aacBitRatePerChannel;
}

void AudioParams::setAacQualityProfile(AacQualityProfile profile) {
    aacQualityProfile = profile;
}

AacQualityProfile AudioParams::getAacQualityProfile() const {
    return aacQualityProfile;
}

void AudioParams::setUsrFrmDepth(int depth) {
    this->usrFrmDepth = depth;
}

int AudioParams::getUsrFrmDepth() const {
    return usrFrmDepth;
}
