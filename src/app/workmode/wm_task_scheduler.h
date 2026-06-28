#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace app_workmode {

enum class WmMode { CaptureOnly = 0, CaptureUpload = 1, UploadOnly = 2 };

enum class TaskType {
    Capture = 1,
    Upload = 2,
};

enum class TaskState {
    Empty = 0,
    Ready,
    Running,
    Stopping,
    Done,
    Failed,
};

enum class ShutdownReason {
    None = 0,
    IdleGrace,
    UploadTimeout,
    Signal,
};

enum class WmSchedulerTraceOp {
    TaskState = 0,
    SlotPut,
    SlotRemove,
    SlotTrigger,
    SlotLock,
    TriggerAccepted,
    TriggerIgnored,
    ShutdownRequested,
};

struct WmTaskSchedulerTrace {
    WmSchedulerTraceOp op = WmSchedulerTraceOp::TaskState;
    TaskType type = TaskType::Capture;
    TaskType targetType = TaskType::Capture;
    TaskState from = TaskState::Empty;
    TaskState to = TaskState::Empty;
    ShutdownReason shutdownReason = ShutdownReason::None;
    int taskId = 0;
    int64_t nowMs = 0;
    const char* reason = "";
};

class WmTask {
public:
    virtual ~WmTask() = default;

    virtual TaskType type() const = 0;
    virtual TaskState state() const = 0;
    virtual int traceId() const { return 0; }
    virtual bool start() = 0;
    virtual void poll(int64_t nowMs) = 0;
    virtual void stop() = 0;

    virtual int64_t timeoutAgeMs(int64_t /*nowMs*/, int64_t defaultAgeMs) const {
        return defaultAgeMs;
    }
};

struct WmTaskSchedulerConfig {
    WmMode mode = WmMode::CaptureOnly;
    int64_t idleGraceMs = 30000;
    int64_t uploadTimeoutMs = 60000;
    bool oneShot = false;
};

struct WmTaskSchedulerEvents {
    bool captureStarted = false;
    bool captureCompleted = false;
    bool uploadStarted = false;
    bool uploadCompleted = false;
    bool uploadTimedOut = false;
    bool shutdownReady = false;
};

class TaskSlot {
public:
    explicit TaskSlot(TaskType type);

    bool put(std::unique_ptr<WmTask> task);
    bool start(int64_t nowMs);
    void poll(int64_t nowMs, bool allowComplete = true);
    void stop();
    void clear();
    void lock();

    bool isLocked() const { return locked_; }
    bool isEmpty() const { return task_.get() == nullptr; }
    bool isRunning() const { return state_ == TaskState::Running; }
    bool isTerminal() const { return state_ == TaskState::Done || state_ == TaskState::Failed; }
    bool taskFinished() const;
    bool taskSucceeded() const;
    int64_t timeoutAgeMs(int64_t nowMs) const;
    TaskState state() const { return state_; }
    TaskType type() const { return type_; }
    int taskTraceId() const;

    void markDoneFromTask();

private:
    TaskType type_;
    std::unique_ptr<WmTask> task_;
    TaskState state_ = TaskState::Empty;
    bool locked_ = false;
    int64_t startedAtMs_ = 0;
};

class WmTaskSchedulerCore {
public:
    using TaskFactory = std::function<std::unique_ptr<WmTask>()>;
    using TraceCallback = std::function<void(const WmTaskSchedulerTrace&)>;

    explicit WmTaskSchedulerCore(const WmTaskSchedulerConfig& config);

    void setCaptureFactory(TaskFactory factory);
    void setUploadFactory(TaskFactory factory);
    void setTraceCallback(TraceCallback callback);

    void bootstrap(int64_t nowMs);
    bool onExternalCaptureTrigger(int64_t nowMs);
    WmTaskSchedulerEvents tick(int64_t nowMs);
    void requestShutdown(ShutdownReason reason);
    void stopAll();

    bool canAcceptCaptureTrigger() const;
    bool hasTask(TaskType type) const;
    TaskState state(TaskType type) const;
    bool isLocked() const;
    bool shouldShutdown() const { return shutdownReason_ != ShutdownReason::None; }
    ShutdownReason shutdownReason() const { return shutdownReason_; }
    int ignoredCaptureTriggers() const { return ignoredCaptureTriggers_; }
    int startedCount(TaskType type) const;
    int completedCount(TaskType type) const;

private:
    TaskSlot& slot(TaskType type);
    const TaskSlot& slot(TaskType type) const;
    bool scheduleCapture(int64_t nowMs, WmTaskSchedulerEvents* events);
    bool scheduleUpload(int64_t nowMs, WmTaskSchedulerEvents* events);
    bool allSlotsEmpty() const;
    // external capture trigger 的拒绝原因（nullptr=可接受）；供 trigger_ignored 日志精确区分。
    const char* captureTriggerRejectReason() const;
    void trace(WmSchedulerTraceOp op, TaskType type, int taskId, TaskState from,
               TaskState to, int64_t nowMs, const char* reason);
    void traceSlotTrigger(TaskType fromType, TaskType toType, int64_t nowMs, const char* reason);
    void traceShutdown(ShutdownReason reason, int64_t nowMs);
    void traceSlotLock(TaskType type, int64_t nowMs);
    void traceStateChange(TaskType type, int taskId, TaskState from, TaskState to,
                          int64_t nowMs, const char* reason);
    void traceStopSlot(TaskSlot& taskSlot, int64_t nowMs);

    WmTaskSchedulerConfig config_;
    TaskSlot captureSlot_;
    TaskSlot uploadSlot_;
    TaskFactory captureFactory_;
    TaskFactory uploadFactory_;
    TraceCallback traceCallback_;
    bool bootstrapped_ = false;
    bool captureStartedOnce_ = false;
    int ignoredCaptureTriggers_ = 0;
    int captureStartedCount_ = 0;
    int uploadStartedCount_ = 0;
    int captureCompletedCount_ = 0;
    int uploadCompletedCount_ = 0;
    int64_t idleStartMs_ = 0;
    ShutdownReason shutdownReason_ = ShutdownReason::None;
};

}  // namespace app_workmode
