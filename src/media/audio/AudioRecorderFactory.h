#ifndef AUDIO_RECORDER_FACTORY_H
#define AUDIO_RECORDER_FACTORY_H

#include "AudioRecorder.h"
#include "AudioParams.h"

namespace media {

/**
 * 音频录制器工厂类（工厂模式）
 */
class AudioRecorderFactory {
public:
    /**
     * 创建音频录制器实例
     * @param type 录制器类型
     * @return 录制器实例指针，失败返回nullptr
     */
    static IAudioRecorder* createRecorder(const AudioParams& params);
    
    /**
     * 销毁音频录制器实例
     * @param recorder 录制器实例指针
     */
    static void destroyRecorder(IAudioRecorder* recorder);

private:
    /**
     * 加载音频录制器驱动
     * @param type 录制器类型
     * @return 是否加载成功
     */
    static bool loadDriver(AudioDeviceType type);

    /**
     * 音频录制器驱动是否已加载
     */
    static bool driverLoaded[static_cast<int>(AudioDeviceType::MAX)];
};

}  // namespace media

#endif  // AUDIO_RECORDER_FACTORY_H