#include "wm_task_scheduler.h"

#include <utility>

namespace app_workmode {

TaskSlot::TaskSlot(TaskType type) : type_(type) {}

bool TaskSlot::put(std::unique_ptr<WmTask> task) {
    if (locked_ || task_.get() != nullptr || !task || task->type() != type_) return false;
    task_ = std::move(task);
    state_ = TaskState::Ready;
    startedAtMs_ = 0;
    return true;
}

bool TaskSlot::start(int64_t nowMs) {
    if (locked_ || !task_ || state_ != TaskState::Ready) return false;
    state_ = task_->start() ? TaskState::Running : TaskState::Failed;
    startedAtMs_ = nowMs;
    markDoneFromTask();
    return state_ == TaskState::Running;
}

void TaskSlot::poll(int64_t nowMs, bool allowComplete) {
    if (!task_ || (state_ != TaskState::Running && state_ != TaskState::Stopping)) return;
    if (!allowComplete) return;
    task_->poll(nowMs);
    markDoneFromTask();
}

void TaskSlot::markDoneFromTask() {
    if (!task_) return;
    TaskState taskState = task_->state();
    if (taskState == TaskState::Stopping) {
        state_ = TaskState::Stopping;
    } else if (taskState == TaskState::Done || taskState == TaskState::Failed) {
        state_ = taskState;
    }
}

bool TaskSlot::taskFinished() const {
    return task_ && (state_ == TaskState::Done || state_ == TaskState::Failed);
}

bool TaskSlot::taskSucceeded() const {
    return task_ && state_ == TaskState::Done;
}

int TaskSlot::taskTraceId() const {
    return task_ ? task_->traceId() : 0;
}

int64_t TaskSlot::timeoutAgeMs(int64_t nowMs) const {
    if (!task_ || (state_ != TaskState::Running && state_ != TaskState::Stopping)) return 0;
    const int64_t ageMs = (startedAtMs_ > 0 && nowMs >= startedAtMs_) ? (nowMs - startedAtMs_) : 0;
    return task_->timeoutAgeMs(nowMs, ageMs);
}

void TaskSlot::stop() {
    if (task_) {
        state_ = TaskState::Stopping;
        task_->stop();
        markDoneFromTask();
    }
}

void TaskSlot::clear() {
    task_.reset();
    state_ = TaskState::Empty;
    startedAtMs_ = 0;
}

void TaskSlot::lock() {
    locked_ = true;
}

WmTaskSchedulerCore::WmTaskSchedulerCore(const WmTaskSchedulerConfig& config)
    : config_(config),
      captureSlot_(TaskType::Capture),
      uploadSlot_(TaskType::Upload) {}

void WmTaskSchedulerCore::setCaptureFactory(TaskFactory factory) {
    captureFactory_ = std::move(factory);
}

void WmTaskSchedulerCore::setUploadFactory(TaskFactory factory) {
    uploadFactory_ = std::move(factory);
}

void WmTaskSchedulerCore::setTraceCallback(TraceCallback callback) {
    traceCallback_ = std::move(callback);
}

void WmTaskSchedulerCore::bootstrap(int64_t nowMs) {
    if (bootstrapped_) return;
    bootstrapped_ = true;

    if (config_.mode == WmMode::UploadOnly) {
        scheduleUpload(nowMs, nullptr);
        return;
    }

    scheduleCapture(nowMs, nullptr);
}

bool WmTaskSchedulerCore::onExternalCaptureTrigger(int64_t nowMs) {
    if (!canAcceptCaptureTrigger()) {
        ++ignoredCaptureTriggers_;
        trace(WmSchedulerTraceOp::TriggerIgnored, TaskType::Capture, 0, TaskState::Empty,
              TaskState::Empty, nowMs, "slot_busy_or_locked");
        return false;
    }
    if (!scheduleCapture(nowMs, nullptr)) {
        ++ignoredCaptureTriggers_;
        trace(WmSchedulerTraceOp::TriggerIgnored, TaskType::Capture, 0, TaskState::Empty,
              TaskState::Empty, nowMs, "schedule_failed");
        return false;
    }
    idleStartMs_ = 0;
    trace(WmSchedulerTraceOp::TriggerAccepted, TaskType::Capture,
          captureSlot_.taskTraceId(), TaskState::Empty, captureSlot_.state(), nowMs,
          "external_capture");
    return true;
}

WmTaskSchedulerEvents WmTaskSchedulerCore::tick(int64_t nowMs) {
    WmTaskSchedulerEvents events;
    if (shutdownReason_ != ShutdownReason::None) return events;

    const TaskState captureBefore = captureSlot_.state();
    const int captureTaskId = captureSlot_.taskTraceId();
    captureSlot_.poll(nowMs);
    traceStateChange(TaskType::Capture, captureTaskId, captureBefore, captureSlot_.state(), nowMs, "poll");

    const TaskState uploadBefore = uploadSlot_.state();
    const int uploadTaskId = uploadSlot_.taskTraceId();
    uploadSlot_.poll(nowMs, captureSlot_.isEmpty());
    traceStateChange(TaskType::Upload, uploadTaskId, uploadBefore, uploadSlot_.state(), nowMs, "poll");

    const bool captureFinished = captureSlot_.taskFinished();
    const bool uploadFinished = uploadSlot_.taskFinished();
    const bool captureSucceeded = captureSlot_.taskSucceeded();

    if (captureFinished) {
        const int taskId = captureSlot_.taskTraceId();
        const TaskState finishedState = captureSlot_.state();
        captureSlot_.clear();
        ++captureCompletedCount_;
        events.captureCompleted = true;
        trace(WmSchedulerTraceOp::SlotRemove, TaskType::Capture, taskId, finishedState,
              TaskState::Empty, nowMs, "task_terminal");

        if (config_.mode == WmMode::CaptureUpload && captureSucceeded && !uploadSlot_.isLocked()) {
            traceSlotTrigger(TaskType::Capture, TaskType::Upload, nowMs, "capture_done");
            events.uploadStarted = scheduleUpload(nowMs, nullptr);
        }
    }

    if (uploadSlot_.isRunning() && config_.uploadTimeoutMs > 0 &&
        uploadSlot_.timeoutAgeMs(nowMs) > config_.uploadTimeoutMs) {
        events.uploadTimedOut = true;
        requestShutdown(ShutdownReason::UploadTimeout);
        return events;
    }

    if (uploadFinished) {
        const int taskId = uploadSlot_.taskTraceId();
        const TaskState finishedState = uploadSlot_.state();
        uploadSlot_.clear();
        ++uploadCompletedCount_;
        events.uploadCompleted = true;
        trace(WmSchedulerTraceOp::SlotRemove, TaskType::Upload, taskId, finishedState,
              TaskState::Empty, nowMs, "task_terminal");
    }

    if (allSlotsEmpty()) {
        if (idleStartMs_ == 0) idleStartMs_ = nowMs;
        if (nowMs - idleStartMs_ >= config_.idleGraceMs) {
            requestShutdown(ShutdownReason::IdleGrace);
            events.shutdownReady = true;
        }
    } else {
        idleStartMs_ = 0;
    }

    return events;
}

void WmTaskSchedulerCore::requestShutdown(ShutdownReason reason) {
    if (shutdownReason_ != ShutdownReason::None) return;
    shutdownReason_ = reason;
    traceShutdown(reason, 0);
    captureSlot_.lock();
    traceSlotLock(TaskType::Capture, 0);
    uploadSlot_.lock();
    traceSlotLock(TaskType::Upload, 0);
}

void WmTaskSchedulerCore::stopAll() {
    traceStopSlot(captureSlot_, 0);
    traceStopSlot(uploadSlot_, 0);
}

bool WmTaskSchedulerCore::canAcceptCaptureTrigger() const {
    if (shutdownReason_ != ShutdownReason::None) return false;
    if (config_.mode == WmMode::UploadOnly) return false;
    if (config_.oneShot && captureStartedOnce_) return false;
    return captureSlot_.isEmpty() && !captureSlot_.isLocked();
}

bool WmTaskSchedulerCore::hasTask(TaskType type) const {
    return !slot(type).isEmpty();
}

TaskState WmTaskSchedulerCore::state(TaskType type) const {
    return slot(type).state();
}

bool WmTaskSchedulerCore::isLocked() const {
    return captureSlot_.isLocked() && uploadSlot_.isLocked();
}

int WmTaskSchedulerCore::startedCount(TaskType type) const {
    return type == TaskType::Capture ? captureStartedCount_ : uploadStartedCount_;
}

int WmTaskSchedulerCore::completedCount(TaskType type) const {
    return type == TaskType::Capture ? captureCompletedCount_ : uploadCompletedCount_;
}

TaskSlot& WmTaskSchedulerCore::slot(TaskType type) {
    return type == TaskType::Capture ? captureSlot_ : uploadSlot_;
}

const TaskSlot& WmTaskSchedulerCore::slot(TaskType type) const {
    return type == TaskType::Capture ? captureSlot_ : uploadSlot_;
}

bool WmTaskSchedulerCore::scheduleCapture(int64_t nowMs, WmTaskSchedulerEvents* events) {
    if (shutdownReason_ != ShutdownReason::None || !captureFactory_) return false;
    if (!captureSlot_.put(captureFactory_())) return false;
    trace(WmSchedulerTraceOp::SlotPut, TaskType::Capture, captureSlot_.taskTraceId(),
          TaskState::Empty, TaskState::Ready, nowMs, "schedule_capture");
    const bool started = captureSlot_.start(nowMs);
    traceStateChange(TaskType::Capture, captureSlot_.taskTraceId(), TaskState::Ready,
                     captureSlot_.state(), nowMs, started ? "start" : "start_failed");
    if (started) {
        captureStartedOnce_ = true;
        ++captureStartedCount_;
        if (events) events->captureStarted = true;
    }
    return started;
}

bool WmTaskSchedulerCore::scheduleUpload(int64_t nowMs, WmTaskSchedulerEvents* events) {
    if (shutdownReason_ != ShutdownReason::None || !uploadFactory_) return false;
    if (!uploadSlot_.put(uploadFactory_())) return false;
    trace(WmSchedulerTraceOp::SlotPut, TaskType::Upload, uploadSlot_.taskTraceId(),
          TaskState::Empty, TaskState::Ready, nowMs, "schedule_upload");
    const bool started = uploadSlot_.start(nowMs);
    traceStateChange(TaskType::Upload, uploadSlot_.taskTraceId(), TaskState::Ready,
                     uploadSlot_.state(), nowMs, started ? "start" : "start_failed");
    if (started) {
        ++uploadStartedCount_;
        if (events) events->uploadStarted = true;
    }
    return started;
}

bool WmTaskSchedulerCore::allSlotsEmpty() const {
    return captureSlot_.isEmpty() && uploadSlot_.isEmpty();
}

void WmTaskSchedulerCore::trace(WmSchedulerTraceOp op, TaskType type, int taskId,
                                TaskState from, TaskState to, int64_t nowMs,
                                const char* reason) {
    if (!traceCallback_) return;
    WmTaskSchedulerTrace t;
    t.op = op;
    t.type = type;
    t.targetType = type;
    t.from = from;
    t.to = to;
    t.taskId = taskId;
    t.nowMs = nowMs;
    t.reason = reason ? reason : "";
    traceCallback_(t);
}

void WmTaskSchedulerCore::traceSlotTrigger(TaskType fromType, TaskType toType, int64_t nowMs,
                                          const char* reason) {
    if (!traceCallback_) return;
    WmTaskSchedulerTrace t;
    t.op = WmSchedulerTraceOp::SlotTrigger;
    t.type = fromType;
    t.targetType = toType;
    t.nowMs = nowMs;
    t.reason = reason ? reason : "";
    traceCallback_(t);
}

void WmTaskSchedulerCore::traceShutdown(ShutdownReason reason, int64_t nowMs) {
    if (!traceCallback_) return;
    WmTaskSchedulerTrace t;
    t.op = WmSchedulerTraceOp::ShutdownRequested;
    t.shutdownReason = reason;
    t.nowMs = nowMs;
    traceCallback_(t);
}

void WmTaskSchedulerCore::traceSlotLock(TaskType type, int64_t nowMs) {
    trace(WmSchedulerTraceOp::SlotLock, type, 0, slot(type).state(), slot(type).state(),
          nowMs, "shutdown");
}

void WmTaskSchedulerCore::traceStateChange(TaskType type, int taskId, TaskState from,
                                           TaskState to, int64_t nowMs,
                                           const char* reason) {
    if (from == to || taskId == 0) return;
    trace(WmSchedulerTraceOp::TaskState, type, taskId, from, to, nowMs, reason);
}

void WmTaskSchedulerCore::traceStopSlot(TaskSlot& taskSlot, int64_t nowMs) {
    if (taskSlot.isEmpty()) return;
    const TaskState before = taskSlot.state();
    const int taskId = taskSlot.taskTraceId();
    if (before == TaskState::Running) {
        traceStateChange(taskSlot.type(), taskId, before, TaskState::Stopping, nowMs, "stop_all_begin");
    }
    taskSlot.stop();
    traceStateChange(taskSlot.type(), taskId,
                     before == TaskState::Running ? TaskState::Stopping : before,
                     taskSlot.state(), nowMs, "stop_all_done");
}

}  // namespace app_workmode
