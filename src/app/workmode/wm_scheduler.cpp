// WmScheduler — see wm_scheduler.h. Runtime wrapper around the slot scheduler
// core in wm_task_scheduler.{h,cpp}.

#include "wm_scheduler.h"

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <stdio.h>             // snprintf（碰撞后缀 int→string；uClibc 无 std::to_string）
#include <cstring>             // strcmp（persistStrandedTmpDir gate）
#include <string>
#include <sys/stat.h>          // stat（tmpfs 工作目录存在性 / 碰撞探测）
#include <thread>
#include <utility>
#include <vector>

#include "ProcessLifecycle.h"   // app_lifecycle::ProcessLifecycle
#include "Common.h"             // EC_SUCCESS
#include "MgmtServClient.h"     // network::MgmtServClient (m3 heartbeat)
#include "upload_task.h"        // UploadTask (new wm-private type=2 task)
#include "capture_lane.h"       // CaptureLane
#include "pir_trigger.h"        // IPirTrigger
#include "wm_paths.h"           // wmUploadPath
#include "Power.h"              // Power::requestShutdown (upload-timeout -> SIGTERM)
#include "app.h"                // QUICK_SNAP_DIR / SD_CARD_PATH（persist 落卡源/目的）
#include "misc/Misc.h"          // mountSDCard / moveDirectoryRecursive / removeDirectory
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
                         int64_t idleGraceMs, int64_t uploadTimeoutMs,
                         std::vector<std::string> workDirs)
    : lc_(lc), mode_(mode), capture_(std::move(capture)),
      trigger_(std::move(trigger)), mgmtAddr_(std::move(mgmtAddr)), mgmtPort_(mgmtPort),
      idleGraceMs_(idleGraceMs), uploadTimeoutMs_(uploadTimeoutMs),
      workDirs_(std::move(workDirs)) {}

WmScheduler::~WmScheduler() {
    if (capture_) capture_->stop();
}

void WmScheduler::run() {
    if (mode_ == WmMode::Heartbeat) {
        runHeartbeat();
        return;
    }

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
        // m2：扫工作目录列表（wm_app 已构建：quickSnap 目录 + 兜底滞留目录，并 ensureWorkDirDesc
        //   建好 desc）。m1：扫 SD wmUploadPath() 单目录（行为不变）。
        // ⚠️ 每个目录串必须以 '/' 结尾：UploadTask::scanAndUploadOnePass 做 dir + f 无分隔符拼接
        //    （wmUploadPath() 自带 "/"，m2 的工作目录由 wm_app/wm_sweep 归一化补尾斜杠）。
        const bool m2 = (mode_ == WmMode::UploadOnly);
        const std::vector<std::string> uploadDirs = m2 ? workDirs_
                                                       : std::vector<std::string>{wmUploadPath()};
        scheduler.setUploadFactory([this, uploadDirs, m2]() {
            // UploadTask 自扫目录取 desc；wakePort_ 在 capture-Done 时被 push 唤醒重扫。
            // m2 独占工作目录 → exclusiveWorkDirs=true（全部成功后整目录清理）；
            // m1 desc 在共享上传扫描目录 → false（仅删 desc 文件，不动共享目录）。
            return std::unique_ptr<WmTask>(
                new UploadTask(wakePort_, mgmtAddr_, mgmtPort_, uploadDirs, m2, nextTaskId()));
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
    // spec §5.3 落卡钩子：stopAll() 已 join upload 线程（文件静默），可安全 move。
    // 仅 UploadTimeout 且 HTC_WM_SD_FALLBACK!="0" 时把 tmpfs 工作目录落 SD。
    persistStrandedTmpDir(reason);
    if (capture_) {
        capture_->stop();
        capture_.reset();
    }
}

void WmScheduler::runHeartbeat() {
    Logger::log(LogLevel::INFO, "[wm] op=start mode=3 (heartbeat)");
    // spec §2.1 m3：connect + auth + 单次 sendHeartbeat
    if (mgmtAddr_.empty() || mgmtPort_ <= 0) {
        Logger::log(LogLevel::WARNING, "[wm] heartbeat: mgmt server not configured, skip");
        return;   // 进 wm_app 尾序 → MCU 回写 + poweroff
    }
    network::MgmtServClient mgmt(mgmtAddr_, mgmtPort_);
    if (EC_SUCCESS != mgmt.connect(3000)) {
        Logger::log(LogLevel::ERROR, "[wm] heartbeat: connect [%s:%d] failed",
                    mgmtAddr_.c_str(), mgmtPort_);
        return;
    }
    if (EC_SUCCESS != mgmt.authenticate()) {
        Logger::log(LogLevel::ERROR, "[wm] heartbeat: auth failed");
        return;
    }
    // sendHeartbeat：payload 落 /tmp 临时 JSON，走 type-1 文件上传（与 JPG 同流程，
    // 服务器不处理 type=254），upload() 等服务器 ACK 才返回 → 删临时文件。
    int rc = mgmt.sendHeartbeat();   // rc = upload() 结果（EC_SUCCESS = 服务器已 ACK 入库）
    Logger::log(LogLevel::INFO, "[wm] op=heartbeat_sent rc=%d", rc);
    // 不 loop。返回后 wm_app 尾序走 shutdown → writebackMcu → poweroff
}

void WmScheduler::persistStrandedTmpDir(ShutdownReason reason) {
    // spec §4 Q3：仅 clean UploadTimeout 落卡（Signal/IdleGrace 不落卡）。
    if (reason != ShutdownReason::UploadTimeout) return;
    const char* env = std::getenv("HTC_WM_SD_FALLBACK");
    if (env && std::strcmp(env, "0") == 0) return;          // gate（默认开）

    // retry-mount SD：lean S11 若 mount 失败，此处再试一次（落卡时 SD 多半已 ready）。
    // mountSDCard 已含「已挂载则 skip」幂等（读 /proc/mounts，Misc.cpp）。
    if (!Misc::mountSDCard(SD_CARD_PATH)) {
        Logger::log(LogLevel::ERROR,
                    "[wm] persist: SD mount failed, tmpfs work dir lost on reboot");
        return;
    }

    // 落卡源 = workDirs_ 中前缀 QUICK_SNAP_DIR（"/tmp/media/"）且盘上仍存在的目录
    //   （= 未被成功上传整目录清理的 tmpfs 工作目录）。SD 滞留目录前缀不匹配，自然跳过——
    //   它们本就在 SD，无需再落。
    const std::string sdMedia = std::string(SD_CARD_PATH) + "media/";
    for (const std::string& wd : workDirs_) {
        if (wd.rfind(QUICK_SNAP_DIR, 0) != 0) continue;     // 非 tmpfs 工作目录
        struct stat st;
        if (stat(wd.c_str(), &st) != 0) continue;            // 已被成功上传整目录清理

        // 目标 SD_CARD_PATH"media/<basename>/"，碰撞加 _2/_3（resume 的 isTimestampDir 已放宽识别）。
        std::string base = wd;
        while (!base.empty() && base.back() == '/') base.pop_back();
        size_t slash = base.find_last_of('/');
        std::string name = (slash == std::string::npos) ? base : base.substr(slash + 1);
        if (name.empty()) continue;

        std::string dst = sdMedia + name + "/";
        for (int suffix = 2; ; ++suffix) {
            struct stat dstSt;
            if (stat(dst.c_str(), &dstSt) != 0) break;       // 不冲突 → 用此名
            char suf[16];
            snprintf(suf, sizeof(suf), "%d", suffix);   // uClibc 无 std::to_string
            dst = sdMedia + name + "_" + suf + "/";
        }

        // 递归 move（跨 fs：tmpfs→vfat rename 返 EXDEV → copy+delete）。失败（ENOSPC 等）
        // 清半成品目标；tmpfs 源不动（reboot 即失——无 SD 空间/IO 失败无解）。
        if (!Misc::moveDirectoryRecursive(wd, dst)) {
            Logger::log(LogLevel::ERROR,
                        "[wm] persist: move %s -> %s failed, cleanup partial",
                        wd.c_str(), dst.c_str());
            Misc::removeDirectory(dst);
            continue;
        }
        Logger::log(LogLevel::INFO, "[wm] persist: stranded tmpfs dir moved -> %s", dst.c_str());
    }
}

}  // namespace app_workmode
