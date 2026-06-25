// SharedVideo — 进程级 IngenicVideo 单例（见 SharedVideo.h）。
// 单 TU 定义，供 media_recorder(VideoRecorder) + media_snap(ImageSnap) 共享。

#include "SharedVideo.h"

#include "HalProvider.h"   // hal::HalProvider::createVideo
#include "Logger.h"

namespace media {

std::shared_ptr<hal::IVideo> sharedVideo() {
    static std::shared_ptr<hal::IVideo> v = []() -> std::shared_ptr<hal::IVideo> {
        auto vid = hal::HalProvider::createVideo();
        if (vid && vid->init()) {
            return vid;
        }
        Logger::log(LogLevel::ERROR, "sharedVideo: createVideo/init failed");
        return nullptr;
    }();
    return v;
}

}  // namespace media
