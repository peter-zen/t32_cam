#ifndef AUDIO_RECORDER_H
#define AUDIO_RECORDER_H

#include "AudioParams.h"
#include <string>
#include <cstdint>

namespace media {

/**
 * 音频数据回调函数类型
 * @param data 编码后的音频数据
 * @param size 数据大小（字节）
 * @param timestamp 时间戳（毫秒）
 * @param isKeyFrame 是否为关键帧（音频通常为false）
 * @param userData 用户自定义数据
 */
typedef void (*AudioDataCallback)(const uint8_t* data, size_t size, uint64_t timestamp, bool isKeyFrame, void* userData);

class IAudioRecorder {
public:
    virtual ~IAudioRecorder() = default;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool isRecording() const = 0;
    virtual bool recordFor(int durationMs) = 0;
    virtual void setAudioParams(const AudioParams& params) = 0;
    virtual void setRecordFilePath(const std::string& filePath) = 0;
    
    /**
     * 设置音频数据回调函数
     * @param callback 回调函数指针
     * @param userData 用户自定义数据
     */
    virtual void setAudioDataCallback(AudioDataCallback callback, void* userData) = 0;
    
    /**
     * 获取当前音频时间戳（毫秒）
     * @return 当前音频时间戳
     */
    virtual uint64_t getCurrentTimestamp() const = 0;
};
}
#endif // AUDIO_RECORDER_H