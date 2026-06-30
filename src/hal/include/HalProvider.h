#pragma once
#include <memory>
#include "IVideo.h"
#include "IAudio.h"
#include "IGpio.h"
namespace hal {
class HalProvider {
public:
    static std::shared_ptr<IVideo> createVideo();
    static std::shared_ptr<IAudio> createAudio();
    static std::shared_ptr<IVideoControl> createVideoControl();
    static std::shared_ptr<IGpio> createGpio();

    // Slice 1b：进程级 IngenicVideo 单例（从 media::sharedVideo 迁移到 hal 层，正本清源）。
    // 首次调用 createVideo()+init()；永不 exit（进程结束/poweroff 自然清理）。
    // 单 TU 定义于 HalProvider.cpp（hal_video.so），跨 .so 共享——不能用 inline header
    // （每个 .so 会各得一份 static → 多实例 → 双 IMP_System_Init → wedge）。
    static std::shared_ptr<IVideo> sharedVideo();
    // 清掉单例引用（无人持有时 ~IngenicVideo → exit → IMP_System_Exit）；下次 sharedVideo()
    // 重新 init（fresh IMP session）。供 wm cm==1 在 photo/record 间做完整 IMP reset。
    static void resetSharedVideo();
};
}
