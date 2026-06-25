// SharedVideo — 进程级 IngenicVideo 单例（见 SharedVideo.h）。
// 单 TU 定义，供 media_recorder(VideoRecorder) + media_snap(ImageSnap) 共享。
//
// 2026-06-25: 单例改为可 reset。resetSharedVideo() 清掉单例引用 →（无人持有时）
// ~IngenicVideo → exit() → IMP_System_Exit；下次 sharedVideo() 重新 create+init
// （fresh IMP session）。供 cm==1 在 photo/record 之间做完整 IMP reset（逼近
// 「分开 app」的 fresh-session-per-capture），见 capture_lane + HTC_CM1_RESET。

#include "SharedVideo.h"

#include "HalProvider.h"   // hal::HalProvider::createVideo
#include "Logger.h"

#include <mutex>

namespace media {

namespace {
// 进程级唯一 IngenicVideo，惰性创建、可 reset。用 mutex 保护 create/reset 的竞争
// （原 magic-statics 只保护首次 init，不保护 reset 后的再创建）。
std::shared_ptr<hal::IVideo>& singletonRef() {
    static std::shared_ptr<hal::IVideo> v;
    return v;
}
std::mutex& singletonMtx() {
    static std::mutex m;
    return m;
}
}  // namespace

std::shared_ptr<hal::IVideo> sharedVideo() {
    std::lock_guard<std::mutex> lock(singletonMtx());
    auto& v = singletonRef();
    if (!v) {
        auto vid = hal::HalProvider::createVideo();
        if (vid && vid->init()) {
            v = std::move(vid);
        } else {
            Logger::log(LogLevel::ERROR, "sharedVideo: createVideo/init failed");
        }
    }
    return v;
}

void resetSharedVideo() {
    std::lock_guard<std::mutex> lock(singletonMtx());
    // 清掉单例引用。若此时无其它调用方持有 shared_ptr（cm==1 的 photo/record 之间
    // 的预期情况），即触发 ~IngenicVideo → exit() → IMP_System_Exit。下次 sharedVideo()
    // 重新 init（fresh IMP session）。
    Logger::log(LogLevel::INFO, "resetSharedVideo: dropping singleton (will IMP_System_Exit on last ref)");
    singletonRef().reset();
}

}  // namespace media
