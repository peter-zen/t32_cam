#include "HalProvider.h"
#include "ingenic/IngenicVideo.h"
#include "ingenic/IngenicAudio.h"
#include "ingenic/IngenicGpio.h"
#include "simu/SimVideo.h"
#include "simu/SimAudio.h"
#include "simu/SimGpio.h"
#include "Logger.h"
#include <mutex>
namespace hal {
std::shared_ptr<IVideo> HalProvider::createVideo() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimVideo>();
    #else
        return std::make_shared<IngenicVideo>();
    #endif
}
std::shared_ptr<IAudio> HalProvider::createAudio() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimAudio>();
    #else
        return std::make_shared<IngenicAudio>();
    #endif
}
std::shared_ptr<IVideoControl> HalProvider::createVideoControl() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimVideoControl>();
    #else
        return std::make_shared<IngenicVideoControl>();
    #endif
}
std::shared_ptr<IGpio> HalProvider::createGpio() {
    #ifdef BUILD_FOR_SIMULATION
        return std::make_shared<SimGpio>();
    #else
        return std::make_shared<IngenicGpio>();
    #endif
}

// ---- Slice 1b：进程级 IngenicVideo 单例（从 media::sharedVideo 迁移到 hal 层）----
namespace {
// 单 TU 持有（HalProvider.cpp ∈ hal_video.so），跨 .so 共享。惰性创建、可 reset。
// mutex 保护 create/reset 竞争（magic-static 只保护首次 init，不保护 reset 后再创建）。
std::shared_ptr<IVideo>& sharedVideoRef() {
    static std::shared_ptr<IVideo> v;
    return v;
}
std::mutex& sharedVideoMtx() {
    static std::mutex m;
    return m;
}
}  // namespace

std::shared_ptr<IVideo> HalProvider::sharedVideo() {
    std::lock_guard<std::mutex> lock(sharedVideoMtx());
    auto& v = sharedVideoRef();
    if (!v) {
        auto vid = createVideo();
        if (vid && vid->init()) {
            v = std::move(vid);
        } else {
            Logger::log(LogLevel::ERROR, "HalProvider::sharedVideo: createVideo/init failed");
        }
    }
    return v;
}

void HalProvider::resetSharedVideo() {
    std::lock_guard<std::mutex> lock(sharedVideoMtx());
    // 清掉单例引用。若此时无其它调用方持有 shared_ptr（cm==1 的 photo/record 之间预期），
    // 即触发 ~IngenicVideo → exit() → IMP_System_Exit。下次 sharedVideo() 重新 init。
    Logger::log(LogLevel::INFO, "HalProvider::resetSharedVideo: dropping singleton (will IMP_System_Exit on last ref)");
    sharedVideoRef().reset();
}
}
