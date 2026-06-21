#include "event_loop.h"

#include "record_task.h"
#include "pir_trigger.h"
#include "upload_worker.h"
#include "ProcessLifecycle.h"  // app_lifecycle::ProcessLifecycle
#include "Power.h"             // Power::requestShutdown (kill SIGTERM)
#include "Logger.h"

#include <cstdlib>             // getenv/atoi (HTC_TEST_RECORD_COUNT)

namespace app_workmode {

namespace {
constexpr int kPollIntervalMs = 200;  // PIR 等待 + 关机信号检查粒度
}

EventLoop::EventLoop(app_lifecycle::ProcessLifecycle& lc,
                     std::shared_ptr<RecordTask> recordTask,
                     std::shared_ptr<IPirTrigger> pirTrigger,
                     std::shared_ptr<UploadWorker> uploadWorker,
                     int64_t uploadTimeoutMs)
    : lc_(lc), recordTask_(std::move(recordTask)), pirTrigger_(std::move(pirTrigger)),
      uploadWorker_(std::move(uploadWorker)), uploadTimeoutMs_(uploadTimeoutMs) {}

void EventLoop::run() {
    Logger::log(LogLevel::INFO, "EventLoop: started (PIR-driven; shutdown on upload-done/timeout/signal)");
    bool hasRecorded = false;       // 曾成功触发过录影（避免启动即关：启动时队列空+无录影）
    bool shutdownRequested = false; // 已 requestShutdown，等 SIGTERM 生效，避免重复请求

    // 测试门控：HTC_TEST_RECORD_COUNT=N 时，upload-idle 关机须等到录满 N 段才生效
    // （devtest 无 upload 后端，否则 EventLoop 录完 #1 即 shutdown，验不到多段）。
    // 默认 1 = 生产行为不变（关机仍由 upload-idle 决定，count 门控恒过）。
    targetRecordCount_ = 1;
    if (const char* e = std::getenv("HTC_TEST_RECORD_COUNT")) {
        int v = std::atoi(e);
        if (v > 0) targetRecordCount_ = v;
    }
    if (targetRecordCount_ > 1) {
        Logger::log(LogLevel::INFO, "EventLoop: test mode — record %d segments before shutdown",
                    targetRecordCount_);
    }

    // 启动立即录一次（-wm 0 工作模式：不等 PIR，尽快开录）。之后才进 PIR 驱动循环。
    if (lc_.keepRunning() && recordTask_->trigger()) {
        hasRecorded = true;
    }

    while (lc_.keepRunning()) {
        if (!shutdownRequested && pirTrigger_->waitForTrigger(kPollIntervalMs) == 1) {
            // 串行录影：is_recording_ 拒并发，重叠触发被丢弃（SDK 不支持并发主码流）。
            if (recordTask_->trigger()) {
                hasRecorded = true;
            }
        }

        // 1) 外部信号（MCU 通知 / 手动 kill）
        if (lc_.waitForSignal(0) != 0) {
            Logger::log(LogLevel::INFO, "EventLoop: external shutdown signal received");
            break;
        }
        if (shutdownRequested) continue;  // 已请求关机，等下一轮 waitForSignal 抓到 SIGTERM

        // 2) 上传 timeout（有未完成上传 + 超时）→ 关机
        if (!uploadWorker_->isIdle()) {
            int64_t age = uploadWorker_->firstEnqueueAgeMs();
            if (age > uploadTimeoutMs_) {
                Logger::log(LogLevel::WARNING,
                    "EventLoop: upload timeout (%lldms > %lldms), shutdown",
                    static_cast<long long>(age), static_cast<long long>(uploadTimeoutMs_));
                shutdownRequested = true;
                Power::getInstance()->requestShutdown();
                continue;
            }
        }

        // 3) 上传完成（曾录过 + 无活跃录影 + 队列空且无在途上传）→ 关机
        //    用 isIdle() 而非 queueEmpty()：后者在 worker pop 后即真，但上传仍在途。
        //    count 门控：HTC_TEST_RECORD_COUNT>1 时须录满 N 段才允许此关机（测试多段用）。
        if (hasRecorded && !recordTask_->isRecording() && uploadWorker_->isIdle()
            && recordTask_->completedCount() >= targetRecordCount_) {
            Logger::log(LogLevel::INFO, "EventLoop: upload complete (idle), shutdown");
            shutdownRequested = true;
            Power::getInstance()->requestShutdown();
            continue;
        }
    }
    Logger::log(LogLevel::INFO, "EventLoop: stopped");
}

}  // namespace app_workmode
