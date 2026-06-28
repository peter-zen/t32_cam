// WmScheduler — see wm_scheduler.h. Runtime wrapper around the slot scheduler
// core in wm_task_scheduler.{h,cpp}.

#include "wm_scheduler.h"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "ProcessLifecycle.h"   // app_lifecycle::ProcessLifecycle
#include "upload_task.h"        // UploadTask (new wm-private type=2 task)
#include "capture_lane.h"       // CaptureLane
#include "pir_trigger.h"        // IPirTrigger
#include "wm_paths.h"           // wmUploadPath
#include "Power.h"              // Power::requestShutdown (upload-timeout -> SIGTERM)
#include "Logger.h"

namespace app_workmode {

namespace {

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

const char* taskStateName(TaskState state) {
    switch (state) {
    case TaskState::Empty: return "Empty";
    case TaskState::Ready: return "Ready";
    case TaskState::Running: return "Running";
    case TaskState::Stopping: return "Stopping";
    case TaskState::Done: return "Done";
    case TaskState::Failed: return "Failed";
    }
    return "Unknown";
}

const char* shutdownReasonName(ShutdownReason reason) {
    switch (reason) {
    case ShutdownReason::None: return "none";
    case ShutdownReason::IdleGrace: return "idle-grace";
    case ShutdownReason::UploadTimeout: return "upload-timeout";
    case ShutdownReason::Signal: return "signal";
    }
    return "unknown";
}

int taskTypeValue(TaskType type) {
    return static_cast<int>(type);
}

int nextTaskId() {
    static std::atomic<int> nextId{1};
    return nextId.fetch_add(1);
}

void logTaskState(TaskType type, int taskId, TaskState from, TaskState to, const char* reason) {
    Logger::log(LogLevel::INFO,
                "[wm] op=task_state type=%d task_id=%d from=%s to=%s reason=%s",
                taskTypeValue(type), taskId, taskStateName(from), taskStateName(to),
                reason ? reason : "");
}

void logSchedulerTrace(const WmTaskSchedulerTrace& trace) {
    switch (trace.op) {
    case WmSchedulerTraceOp::TaskState:
        logTaskState(trace.type, trace.taskId, trace.from, trace.to, trace.reason);
        break;
    case WmSchedulerTraceOp::SlotPut:
        Logger::log(LogLevel::INFO,
                    "[wm] op=slot_put type=%d task_id=%d state=%s reason=%s",
                    taskTypeValue(trace.type), trace.taskId, taskStateName(trace.to),
                    trace.reason ? trace.reason : "");
        break;
    case WmSchedulerTraceOp::SlotRemove:
        Logger::log(LogLevel::INFO,
                    "[wm] op=slot_remove type=%d task_id=%d final_state=%s reason=%s",
                    taskTypeValue(trace.type), trace.taskId, taskStateName(trace.from),
                    trace.reason ? trace.reason : "");
        break;
    case WmSchedulerTraceOp::SlotTrigger:
        Logger::log(LogLevel::INFO,
                    "[wm] op=slot_trigger from=%d to=%d reason=%s",
                    taskTypeValue(trace.type), taskTypeValue(trace.targetType),
                    trace.reason ? trace.reason : "");
        break;
    case WmSchedulerTraceOp::SlotLock:
        Logger::log(LogLevel::INFO,
                    "[wm] op=slot_lock type=%d state=%s reason=%s",
                    taskTypeValue(trace.type), taskStateName(trace.to),
                    trace.reason ? trace.reason : "");
        break;
    case WmSchedulerTraceOp::TriggerAccepted:
        Logger::log(LogLevel::INFO,
                    "[wm] op=trigger_accepted type=%d task_id=%d state=%s reason=%s",
                    taskTypeValue(trace.type), trace.taskId, taskStateName(trace.to),
                    trace.reason ? trace.reason : "");
        break;
    case WmSchedulerTraceOp::TriggerIgnored:
        Logger::log(LogLevel::INFO,
                    "[wm] op=trigger_ignored type=%d reason=%s",
                    taskTypeValue(trace.type), trace.reason ? trace.reason : "");
        break;
    case WmSchedulerTraceOp::ShutdownRequested:
        Logger::log(LogLevel::INFO,
                    "[wm] op=shutdown_requested reason=%s",
                    shutdownReasonName(trace.shutdownReason));
        break;
    }
}

class CaptureLaneTask : public WmTask {
public:
    explicit CaptureLaneTask(std::shared_ptr<CaptureLane> capture)
        : capture_(std::move(capture)), taskId_(nextTaskId()) {}

    ~CaptureLaneTask() override {
        stop();
    }

    TaskType type() const override { return TaskType::Capture; }
    TaskState state() const override { return state_.load(); }
    int traceId() const override { return taskId_; }

    bool start() override {
        TaskState expected = TaskState::Ready;
        if (!state_.compare_exchange_strong(expected, TaskState::Running)) return false;
        worker_ = std::thread([this]() {
            bool ok = capture_ && capture_->runOnceBlocking(stopRequested_);
            ok_.store(ok);
            state_.store(ok ? TaskState::Done : TaskState::Failed);
        });
        return true;
    }

    void poll(int64_t /*nowMs*/) override {
        joinIfFinished();
    }

    void stop() override {
        TaskState s = state_.load();
        if (s == TaskState::Running) {
            stopRequested_.store(true);
            state_.store(TaskState::Stopping);
        }
        if (worker_.joinable()) worker_.join();
        if (state_.load() == TaskState::Stopping) {
            state_.store(ok_.load() ? TaskState::Done : TaskState::Failed);
        }
    }

private:
    void joinIfFinished() {
        TaskState s = state_.load();
        if ((s == TaskState::Done || s == TaskState::Failed) && worker_.joinable()) {
            worker_.join();
        }
    }

    std::shared_ptr<CaptureLane> capture_;
    std::thread worker_;
    std::atomic<TaskState> state_{TaskState::Ready};
    std::atomic<bool> ok_{false};
    std::atomic<bool> stopRequested_{false};
    int taskId_ = 0;
};

// UploadTask（新 wm 私有 type=2 task，自扫 SD + SlotOutputPort 唤醒）见 upload_task.{h,cpp}。

}  // namespace

WmScheduler::WmScheduler(app_lifecycle::ProcessLifecycle& lc, WmMode mode,
                         std::shared_ptr<CaptureLane> capture,
                         std::shared_ptr<IPirTrigger> trigger,
                         std::string mgmtAddr, int mgmtPort,
                         int64_t idleGraceMs, int64_t uploadTimeoutMs)
    : lc_(lc), mode_(mode), capture_(std::move(capture)),
      trigger_(std::move(trigger)), mgmtAddr_(std::move(mgmtAddr)), mgmtPort_(mgmtPort),
      idleGraceMs_(idleGraceMs), uploadTimeoutMs_(uploadTimeoutMs) {}

WmScheduler::~WmScheduler() {
    if (capture_) capture_->stop();
}

void WmScheduler::run() {
    const int kPollMs = 200;
    const bool oneShot = (std::getenv("HTC_WM_ONE_SHOT") != nullptr);

    WmTaskSchedulerConfig cfg;
    cfg.mode = mode_;
    cfg.idleGraceMs = idleGraceMs_;
    cfg.uploadTimeoutMs = uploadTimeoutMs_;
    cfg.oneShot = oneShot;

    WmTaskSchedulerCore scheduler(cfg);
    scheduler.setTraceCallback(logSchedulerTrace);
    if (capture_) {
        scheduler.setCaptureFactory([this]() {
            return std::unique_ptr<WmTask>(new CaptureLaneTask(capture_));
        });
    }
    if (mode_ != WmMode::CaptureOnly) {
        const std::string uploadDir = wmUploadPath();
        scheduler.setUploadFactory([this, uploadDir]() {
            // UploadTask 自扫 SD 取 desc；wakePort_ 在 capture-Done 时被 push 唤醒重扫。
            return std::unique_ptr<WmTask>(
                new UploadTask(wakePort_, mgmtAddr_, mgmtPort_, uploadDir, nextTaskId()));
        });
    }

    Logger::log(LogLevel::INFO, "[wm] op=start mode=%d oneShot=%d",
                static_cast<int>(mode_), oneShot ? 1 : 0);

    scheduler.bootstrap(steadyNowMs());

    while (!scheduler.shouldShutdown()) {
        const int64_t nowMs = steadyNowMs();
        WmTaskSchedulerEvents events = scheduler.tick(nowMs);
        if (events.uploadTimedOut) {
            Logger::log(LogLevel::WARNING,
                        "[wm] op=timeout upload_timeout_ms=%lld, requestShutdown",
                        (long long)uploadTimeoutMs_);
            Power::getInstance()->requestShutdown();
            break;
        }
        // capture 完成时 push wake token：唤醒空闲等待中的 UploadTask 重扫 SD
        // （slot2 未运行时 scheduler 已在同一 tick 新建 UploadTask，token 留给后续拍）。
        if (events.captureCompleted) {
            wakePort_.push();
            Logger::log(LogLevel::INFO, "[wm] op=slot_port_push type=1 port=upload count=1");
        }

        bool waitedForTrigger = false;
        if (mode_ != WmMode::UploadOnly && trigger_ && scheduler.canAcceptCaptureTrigger()) {
            waitedForTrigger = true;
            if (trigger_->waitForTrigger(kPollMs) == 1) {
                scheduler.onExternalCaptureTrigger(steadyNowMs());
            }
        } else if (mode_ != WmMode::UploadOnly && trigger_ && !scheduler.canAcceptCaptureTrigger()) {
            waitedForTrigger = true;
            if (trigger_->waitForTrigger(kPollMs) == 1) {
                scheduler.onExternalCaptureTrigger(steadyNowMs());
            }
        }

        if (lc_.waitForSignal(waitedForTrigger ? 0 : kPollMs) != 0) {
            scheduler.requestShutdown(ShutdownReason::Signal);
            break;
        }
    }

    ShutdownReason reason = scheduler.shutdownReason();
    if (reason == ShutdownReason::IdleGrace) {
        Logger::log(LogLevel::INFO, "[wm] op=shutdown reason=idle-grace mode=%d",
                    static_cast<int>(mode_));
    } else if (reason == ShutdownReason::Signal || lc_.waitForSignal(0) != 0) {
        Logger::log(LogLevel::INFO, "[wm] op=shutdown reason=signal mode=%d",
                    static_cast<int>(mode_));
    } else if (reason == ShutdownReason::UploadTimeout) {
        Logger::log(LogLevel::INFO, "[wm] op=shutdown reason=upload-timeout mode=%d",
                    static_cast<int>(mode_));
    }

    scheduler.stopAll();
    if (capture_) {
        capture_->stop();
        capture_.reset();
    }
}

}  // namespace app_workmode
