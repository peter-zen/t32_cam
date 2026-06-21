#pragma once

#include <cstdint>
#include <memory>

namespace app_lifecycle { class ProcessLifecycle; }

namespace app_workmode {

class RecordTask;
class IPirTrigger;
class UploadWorker;

// 长驻主循环（阶段3+4）：PIR 触发 → RecordTask::trigger（异步录影，完时 enqueue 上传）；
// 三路关机：
//   1) 外部信号（MCU 通知 / 手动 kill）→ waitForSignal 检测 → break
//   2) 上传完成（曾录过 + 无活跃录影 + 上传队列空）→ requestShutdown
//   3) 上传 timeout（队列非空 + 超 uploadTimeoutMs）→ requestShutdown
// requestShutdown 发 SIGTERM，下一轮 waitForSignal 触发 cleanupHook + break。
class EventLoop {
public:
    EventLoop(app_lifecycle::ProcessLifecycle& lc,
              std::shared_ptr<RecordTask> recordTask,
              std::shared_ptr<IPirTrigger> pirTrigger,
              std::shared_ptr<UploadWorker> uploadWorker,
              int64_t uploadTimeoutMs);

    // 阻塞直到关机（收到 SIGTERM，keepRunning()==false）。
    void run();

private:
    app_lifecycle::ProcessLifecycle& lc_;
    std::shared_ptr<RecordTask> recordTask_;
    std::shared_ptr<IPirTrigger> pirTrigger_;
    std::shared_ptr<UploadWorker> uploadWorker_;
    int64_t uploadTimeoutMs_;
};

}  // namespace app_workmode
