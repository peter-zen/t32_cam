#ifndef AUDIO_PARAMS_H
#define AUDIO_PARAMS_H

#include <string>

namespace media {

/**
 * 音频编码格式枚举
 */
enum class AudioCodecFormat {
    G711A,  // G711 A-law 编码
    G711U,  // G711 μ-law 编码
    AAC     // AAC 编码
};

/**
 * 音频采样率枚举
 */
enum class AudioSampleRate {
    SR_8000,   // 8000 Hz
    SR_16000,  // 16000 Hz
    SR_48000,   // 48000 Hz
    MAX        // 最大采样率数量
};

/**
 * 音频设备类型枚举
 */
enum class AudioDeviceType {
    AUDIO_IN,      // Audio In设备
    DMIC_IN,     // DMIC设备
    MAX          // 最大设备类型数量
};

/**
 * 音频位宽枚举
 */
enum class AudioBitWidth {
    BW_16,   // 16位
    MAX      // 最大位宽数量
};

/**
 * 音频声道模式枚举
 */
enum class AudioSoundMode {
    MONO,     // 单声道
    STEREO,    // 立体声
    MAX       // 最大声道模式数量
};

enum class AacQualityProfile {
    VOICE,
    ENVIRONMENT,
    MUSIC_HIGH,
    MAX
};

/**
 * 音频参数类
 */
class AudioParams {
public:
    AudioParams();
    AudioParams(const AudioParams& other) = default;
    ~AudioParams() = default;

    /**
     * 设置音频设备类型
     */
    void setDeviceType(AudioDeviceType type);
    AudioDeviceType getDeviceType() const;

    /**
     * 设置音频编码格式
     */
    void setCodecFormat(AudioCodecFormat format);
    AudioCodecFormat getCodecFormat() const;

    /**
     * 设置音频采样率
     */
    void setSampleRate(AudioSampleRate rate);
    AudioSampleRate getSampleRate() const;
    int getSampleRateValue() const;  // 获取采样率的数值

    /**
     * 设置音频音量
     * @param volume 音量范围: [-30 ~ 120]，-30表示静音，120表示放大30dB，步长0.5dB
     */
    void setVolume(int volume);
    int getVolume() const;

    /**
     * 设置音频增益
     * @param gain 增益范围: [0 ~ 31]，12为临界点，步长1.5dB
     */
    void setGain(int gain);
    int getGain() const;

    /**
     * 设置设备ID
     */
    void setDeviceId(int devId);
    int getDeviceId() const;

    /**
     * 设置通道ID
     */
    void setChannelId(int chnId);
    int getChannelId() const;

    int getNumPerFrame() const;

    int getFrameNum() const;

    /**
     * 设置通道数量(仅DMIC设备使用)
     */
    void setChannelCount(int chnCnt);
    int getChannelCount() const;

    /**
     * 设置DMIC配置参数
     * @param aecDmicId AEC麦克风ID
     * @param needAec 是否需要AEC
     */
    void setDmicConfig(int aecDmicId, bool needAec);
    int getAecDmicId() const;
    bool isNeedAec() const;
    
    /**
     * 设置音频位宽
     */
    void setBitWidth(AudioBitWidth bitWidth);
    AudioBitWidth getBitWidth() const;
    int getBitWidthValue() const;  // 获取位宽的数值
    
    /**
     * 设置音频声道模式
     */
    void setSoundMode(AudioSoundMode soundMode);
    AudioSoundMode getSoundMode() const;
    int getSoundModeValue() const;  // 获取声道模式的数值
    
    void setAacBitRatePerChannel(unsigned long bitRate);
    unsigned long getAacBitRatePerChannel() const;
    void setAacQualityProfile(AacQualityProfile profile);
    AacQualityProfile getAacQualityProfile() const;
    
    /**
     * 设置用户帧深度
     */
    void setUsrFrmDepth(int depth);
    int getUsrFrmDepth() const;

private:
    AudioDeviceType deviceType;      // 音频设备类型
    AudioCodecFormat codecFormat;    // 编码格式
    AudioSampleRate sampleRate;      // 采样率
    AudioBitWidth bitWidth;          // 音频位宽
    AudioSoundMode soundMode;        // 音频声道模式
    int volume;                      // 音量
    int gain;                        // 增益
    
    // 通用设备参数
    int deviceId;                    // 设备ID
    int channelId;                   // 通道ID
    int numPerFrame;                 // 每帧采样数
    int frameNum;                    // 缓存帧数
    int usrFrmDepth;                 // 用户帧深度
    
    // DMIC特有参数
    int channelCount;                // 通道数量
    int aecDmicId;                   // AEC麦克风ID
    bool needAec;                    // 是否需要AEC
    unsigned long aacBitRatePerChannel;
    AacQualityProfile aacQualityProfile;
};

}  // namespace media

#endif  // AUDIO_PARAMS_H
