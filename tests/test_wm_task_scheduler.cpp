#include "wm_task_scheduler.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

using namespace app_workmode;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

class MockTask : public WmTask {
public:
    MockTask(TaskType type, int pollsToFinish)
        : type_(type), pollsToFinish_(pollsToFinish) {}

    TaskType type() const override { return type_; }
    TaskState state() const override { return state_; }

    bool start() override {
        state_ = TaskState::Running;
        return true;
    }

    void poll(int64_t nowMs) override {
        (void)nowMs;
        if (state_ != TaskState::Running) return;
        ++polls_;
        if (polls_ >= pollsToFinish_) state_ = success_ ? TaskState::Done : TaskState::Failed;
    }

    void stop() override {
        stopped_ = true;
        state_ = success_ ? TaskState::Done : TaskState::Failed;
    }

    void setSuccess(bool success) { success_ = success; }
    bool stopped() const { return stopped_; }

private:
    TaskType type_;
    int pollsToFinish_;
    int polls_ = 0;
    TaskState state_ = TaskState::Ready;
    bool success_ = true;
    bool stopped_ = false;
};

class DelayedCleanupCaptureTask : public WmTask {
public:
    TaskType type() const override { return TaskType::Capture; }
    TaskState state() const override { return state_; }
    bool start() override {
        state_ = TaskState::Running;
        return true;
    }
    void poll(int64_t /*nowMs*/) override {
        if (state_ == TaskState::Running) {
            state_ = TaskState::Stopping;
        } else if (state_ == TaskState::Stopping) {
            state_ = TaskState::Done;
        }
    }
    void stop() override {
        state_ = TaskState::Done;
    }

private:
    TaskState state_ = TaskState::Ready;
};

class UploadWaitsForCaptureTask : public WmTask {
public:
    TaskType type() const override { return TaskType::Upload; }
    TaskState state() const override { return state_; }
    bool start() override {
        state_ = TaskState::Running;
        return true;
    }
    void poll(int64_t /*nowMs*/) override { state_ = TaskState::Done; }
    void stop() override { state_ = TaskState::Done; }

private:
    TaskState state_ = TaskState::Ready;
};

class TimeoutUploadTask : public WmTask {
public:
    explicit TimeoutUploadTask(int64_t ageMs) : ageMs_(ageMs) {}
    TaskType type() const override { return TaskType::Upload; }
    TaskState state() const override { return state_; }
    bool start() override {
        state_ = TaskState::Running;
        return true;
    }
    void poll(int64_t /*nowMs*/) override {}
    void stop() override { state_ = TaskState::Done; }
    int64_t timeoutAgeMs(int64_t /*nowMs*/, int64_t /*defaultAgeMs*/) const override {
        return ageMs_;
    }

private:
    int64_t ageMs_;
    TaskState state_ = TaskState::Ready;
};

WmTaskSchedulerConfig config(WmMode mode) {
    WmTaskSchedulerConfig cfg;
    cfg.mode = mode;
    cfg.idleGraceMs = 100;
    cfg.uploadTimeoutMs = 500;
    cfg.oneShot = false;
    return cfg;
}

void test_capture_only_shutdown() {
    WmTaskSchedulerCore scheduler(config(WmMode::CaptureOnly));
    scheduler.setCaptureFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Capture, 1));
    });

    scheduler.bootstrap(1000);
    require(scheduler.state(TaskType::Capture) == TaskState::Running, "m0 capture starts at bootstrap");
    scheduler.tick(1060);
    require(!scheduler.hasTask(TaskType::Capture), "m0 capture removed after completion");
    require(scheduler.completedCount(TaskType::Capture) == 1, "m0 capture completion counted");
    scheduler.tick(1161);
    require(scheduler.shouldShutdown(), "m0 idle-grace enters shutdown");
    require(scheduler.shutdownReason() == ShutdownReason::IdleGrace, "m0 shutdown reason is idle-grace");
}

void test_upload_only_starts_immediately() {
    WmTaskSchedulerCore scheduler(config(WmMode::UploadOnly));
    scheduler.setUploadFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Upload, 1));
    });

    scheduler.bootstrap(2000);
    require(scheduler.state(TaskType::Upload) == TaskState::Running, "m2 upload starts at bootstrap");
    scheduler.tick(2020);
    require(!scheduler.hasTask(TaskType::Upload), "m2 upload removed after completion");
    scheduler.tick(2121);
    require(scheduler.shouldShutdown(), "m2 idle-grace enters shutdown");
}

void test_capture_upload_ordering() {
    WmTaskSchedulerCore scheduler(config(WmMode::CaptureUpload));
    scheduler.setCaptureFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Capture, 1));
    });
    scheduler.setUploadFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Upload, 1));
    });

    scheduler.bootstrap(3000);
    require(scheduler.state(TaskType::Capture) == TaskState::Running, "m1 capture starts first");
    require(!scheduler.hasTask(TaskType::Upload), "m1 upload waits for capture");

    WmTaskSchedulerEvents events = scheduler.tick(3060);
    require(events.captureCompleted, "m1 capture completion event emitted");
    require(events.uploadStarted, "m1 capture completion triggers upload");
    require(scheduler.state(TaskType::Upload) == TaskState::Running, "m1 upload slot running");

    scheduler.tick(3100);
    require(!scheduler.hasTask(TaskType::Upload), "m1 upload removed after completion");
}

void test_upload_waits_until_capture_cleanup_done() {
    WmTaskSchedulerCore scheduler(config(WmMode::CaptureUpload));
    scheduler.setCaptureFactory([]() {
        return std::unique_ptr<WmTask>(new DelayedCleanupCaptureTask());
    });
    scheduler.setUploadFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Upload, 1));
    });

    scheduler.bootstrap(3500);
    WmTaskSchedulerEvents events = scheduler.tick(3510);
    require(!events.uploadStarted, "upload must not start while capture is stopping/cleaning up");
    require(scheduler.state(TaskType::Capture) == TaskState::Stopping, "capture remains in stopping state");

    events = scheduler.tick(3520);
    require(events.captureCompleted, "capture completion emitted after cleanup done");
    require(events.uploadStarted, "upload starts only after capture cleanup is done");
}

void test_busy_capture_trigger_is_ignored() {
    WmTaskSchedulerCore scheduler(config(WmMode::CaptureUpload));
    scheduler.setCaptureFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Capture, 3));
    });

    scheduler.bootstrap(4000);
    bool accepted = scheduler.onExternalCaptureTrigger(4010);
    require(!accepted, "busy capture slot rejects external trigger");
    require(scheduler.ignoredCaptureTriggers() == 1, "ignored capture trigger counted");
    require(scheduler.startedCount(TaskType::Capture) == 1, "no second capture started while slot occupied");
}

void test_trigger_during_upload_when_capture_empty() {
    WmTaskSchedulerCore scheduler(config(WmMode::CaptureUpload));
    int uploadPolls = 0;
    scheduler.setCaptureFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Capture, 1));
    });
    scheduler.setUploadFactory([&uploadPolls]() {
        ++uploadPolls;
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Upload, 5));
    });

    scheduler.bootstrap(5000);
    scheduler.tick(5020);
    require(scheduler.state(TaskType::Upload) == TaskState::Running, "upload running after first capture");

    bool accepted = scheduler.onExternalCaptureTrigger(5030);
    require(accepted, "capture trigger accepted while upload is running and slot 1 is empty");
    require(scheduler.startedCount(TaskType::Capture) == 2, "second capture started during upload");
    require(scheduler.state(TaskType::Capture) == TaskState::Running, "second capture is running during upload");
    require(scheduler.state(TaskType::Upload) == TaskState::Running, "upload remains running with second capture");
    require(uploadPolls == 1, "second capture trigger must not create another upload task");
}

void test_upload_waits_for_capture_slot_before_finishing() {
    WmTaskSchedulerCore scheduler(config(WmMode::CaptureUpload));
    scheduler.setCaptureFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Capture, 2));
    });
    scheduler.setUploadFactory([]() {
        return std::unique_ptr<WmTask>(new UploadWaitsForCaptureTask());
    });

    scheduler.bootstrap(6000);
    scheduler.tick(6110);
    scheduler.tick(6120);
    require(scheduler.state(TaskType::Upload) == TaskState::Running, "upload started after first capture");

    bool accepted = scheduler.onExternalCaptureTrigger(6130);
    require(accepted, "second capture accepted");
    scheduler.tick(6140);
    require(scheduler.hasTask(TaskType::Upload), "upload remains in slot while capture slot is occupied");

    scheduler.tick(6230);
    require(!scheduler.hasTask(TaskType::Capture), "second capture completed");
    scheduler.tick(6240);
    require(!scheduler.hasTask(TaskType::Upload), "upload finishes after capture slot empties");
}

void test_upload_timeout_requests_shutdown() {
    WmTaskSchedulerConfig cfg = config(WmMode::UploadOnly);
    cfg.uploadTimeoutMs = 100;
    WmTaskSchedulerCore scheduler(cfg);
    scheduler.setUploadFactory([]() {
        return std::unique_ptr<WmTask>(new TimeoutUploadTask(150));
    });

    scheduler.bootstrap(7000);
    WmTaskSchedulerEvents events = scheduler.tick(7010);
    require(events.uploadTimedOut, "upload timeout event emitted");
    require(scheduler.shouldShutdown(), "upload timeout requests shutdown");
    require(scheduler.shutdownReason() == ShutdownReason::UploadTimeout, "shutdown reason is upload timeout");
}

void test_shutdown_locks_slots() {
    WmTaskSchedulerCore scheduler(config(WmMode::CaptureOnly));
    scheduler.setCaptureFactory([]() {
        return std::unique_ptr<WmTask>(new MockTask(TaskType::Capture, 1));
    });

    scheduler.bootstrap(8000);
    scheduler.tick(8010);
    scheduler.tick(8120);
    require(scheduler.shouldShutdown(), "idle shutdown reached");
    require(scheduler.isLocked(), "shutdown locks task slots");
    bool accepted = scheduler.onExternalCaptureTrigger(8130);
    require(!accepted, "locked slot rejects later capture trigger");
}

}  // namespace

int main() {
    test_capture_only_shutdown();
    test_upload_only_starts_immediately();
    test_capture_upload_ordering();
    test_upload_waits_until_capture_cleanup_done();
    test_busy_capture_trigger_is_ignored();
    test_trigger_during_upload_when_capture_empty();
    test_upload_waits_for_capture_slot_before_finishing();
    test_upload_timeout_requests_shutdown();
    test_shutdown_locks_slots();

    std::cout << "test_wm_task_scheduler: all tests passed" << std::endl;
    return 0;
}
