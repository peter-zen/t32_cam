#ifndef AUDIO_IN_H
#define AUDIO_IN_H

#include "AudioRecorder.h"
#include "AudioParams.h"
#include "AacEncoder.h"

#include <string>
#include <thread>

namespace media {

#include <memory>

class AudioIn : public IAudioRecorder {
public:
    AudioIn();
    virtual ~AudioIn() override;
    
    virtual bool start() override;
    virtual bool stop() override;
    
    /**
     * 获取录音状态
     */
    virtual bool isRecording() const override;
    
    /**
     * 录音指定时长
     */
    virtual bool recordFor(int durationMs) override;
    
    /**
     * 设置音频参数
     */
    virtual void setAudioParams(const AudioParams& params) override;
    
    /**
     * 设置录音文件路径
     */
    virtual void setRecordFilePath(const std::string& filePath) override;
    
    /**
     * 设置音频数据回调函数
     */
    virtual void setAudioDataCallback(AudioDataCallback callback, void* userData) override;
    
    /**
     * 获取当前音频时间戳（毫秒）
     */
    virtual uint64_t getCurrentTimestamp() const override;
    /**
     * 加载音频输入驱动
     */
    static bool loadDriver();
private:
    void recordThreadFunc();
    bool isRecording_;          // 录音状态
    AudioParams audioParams;   // 音频参数
    std::string recordFilePath;// 录音文件路径
    FILE* recordFile;          // 录音文件指针
    
    std::shared_ptr<std::thread> recordThread; // 录音线程
    std::shared_ptr<std::thread> timerThread;  // 定时器线程
    bool threadRunning;        // 录音线程运行状态
    
    // 音频数据回调相关
    AudioDataCallback audioDataCallback;  // 音频数据回调函数
    void* audioDataCallbackUserData;      // 回调用户数据
    uint64_t currentTimestamp;            // 当前音频时间戳（毫秒）

    AacEncoder aacEncoder;
    bool aencEnabled;
    int aencChn;
    int actualChnCnt;
    int actualSampleRate;
};
}

#endif // AUDIO_IN_H
