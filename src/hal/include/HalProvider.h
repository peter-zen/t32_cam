#pragma once
#include <memory>
#include "IVideo.h"
#include "IAudio.h"
#include "IGpio.h"
namespace hal {
class HalProvider {
public:
    static std::shared_ptr<IVideo> createVideo(const HalVideoConfig& cfg);
    static std::shared_ptr<IAudio> createAudio();
    static std::shared_ptr<IVideoControl> createVideoControl();
    static std::shared_ptr<IGpio> createGpio();

    // 声明本进程的 HAL 常驻通道配置（residentMode/withThumb）。boot 入口在首次 sharedVideo()
    // 之前调一次（quickSnap {0,false} / wm {cm,true}）；未调则用默认 {-1,true}（um/main_app/test
    // 继承，行为同旧）。取代 HTC_HAL_RESIDENT_MODE env（已退役）。仅记 config——单例构造仍是
    // sharedVideo() 首次 lazy 触发（保持 IMP 按需 init 的时序；cm==1 reset→re-init 复用此 config）。
    static void start(const HalVideoConfig& cfg);

    // Slice 1b：进程级 IngenicVideo 单例（从 media::sharedVideo 迁移到 hal 层，正本清源）。
    // 首次调用 createVideo(s_residentCfg)+init()；永不 exit（进程结束/poweroff 自然清理）。
    // 单 TU 定义于 HalProvider.cpp（hal_video.so），跨 .so 共享——不能用 inline header
    // （每个 .so 会各得一份 static → 多实例 → 双 IMP_System_Init → wedge）。
    static std::shared_ptr<IVideo> sharedVideo();
    // 清掉单例引用（无人持有时 ~IngenicVideo → exit → IMP_System_Exit）；下次 sharedVideo()
    // 重新 init（fresh IMP session，复用 start() 设的 config）。供 wm cm==1 在 photo/record 间做完整 IMP reset。
    static void resetSharedVideo();
};
}
