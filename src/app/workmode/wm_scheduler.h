#pragma once
// WmScheduler — wm 的 3 模式任务调度器（wm-app-spec §3）。
//
// 结构：pending(触发) + Capture lane + Upload lane + 终态 Shutdown。Capture/Upload
// 并发不互斥。关机自管：三条 lane 全空持续 idleGraceMs → Shutdown；Upload 超时→
// requestShutdown(SIGTERM)；外部信号→Shutdown。Shutdown 屏蔽触发、不可中断。
//
// m0(CaptureOnly)=仅 Capture lane；m1(CaptureUpload)=Capture+Upload；m2(UploadOnly)=仅 Upload。
#include <cstdint>
#include <memory>

namespace app_lifecycle { class ProcessLifecycle; }

namespace app_workmode {

class UploadWorker;
class CaptureLane;
class IPirTrigger;

enum class WmMode { CaptureOnly = 0, CaptureUpload = 1, UploadOnly = 2 };

class WmScheduler {
public:
    // upload 可为 null（m0 无 upload lane）；capture/trigger 可为 null（m2 无捕获）。
    WmScheduler(app_lifecycle::ProcessLifecycle& lc, WmMode mode,
                std::shared_ptr<UploadWorker> upload,
                std::shared_ptr<CaptureLane> capture,
                std::shared_ptr<IPirTrigger> trigger,
                int64_t idleGraceMs, int64_t uploadTimeoutMs);
    ~WmScheduler();

    // 长驻直到关机（idle-grace/upload-timeout/信号）。run() 末尾 stop capture lane
    // （wm_app 尾随后做 lc.shutdown=IMP teardown）。
    void run();

private:
    app_lifecycle::ProcessLifecycle& lc_;
    WmMode  mode_;
    std::shared_ptr<UploadWorker> upload_;
    std::shared_ptr<CaptureLane>  capture_;
    std::shared_ptr<IPirTrigger> trigger_;
    int64_t idleGraceMs_;
    int64_t uploadTimeoutMs_;
};

}  // namespace app_workmode
