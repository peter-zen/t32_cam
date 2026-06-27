// WmScheduler — see wm_scheduler.h. 3 modes: m0(CaptureOnly)/m1(CaptureUpload)/m2(UploadOnly).

#include "wm_scheduler.h"

#include <chrono>
#include <cstdlib>
#include <string>

#include "ProcessLifecycle.h"   // app_lifecycle::ProcessLifecycle
#include "upload_worker.h"      // UploadWorker
#include "capture_lane.h"       // CaptureLane
#include "pir_trigger.h"        // IPirTrigger
#include "wm_paths.h"
#include "misc/Misc.h"          // Misc::listFilenames
#include "Power.h"              // Power::requestShutdown (upload-timeout -> SIGTERM)
#include "Logger.h"

namespace app_workmode {

namespace {
int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

WmScheduler::WmScheduler(app_lifecycle::ProcessLifecycle& lc, WmMode mode,
                         std::shared_ptr<UploadWorker> upload,
                         std::shared_ptr<CaptureLane> capture,
                         std::shared_ptr<IPirTrigger> trigger,
                         int64_t idleGraceMs, int64_t uploadTimeoutMs)
    : lc_(lc), mode_(mode), upload_(std::move(upload)), capture_(std::move(capture)),
      trigger_(std::move(trigger)), idleGraceMs_(idleGraceMs), uploadTimeoutMs_(uploadTimeoutMs) {}

WmScheduler::~WmScheduler() {
    // run() 末尾已 stop+reset capture_；这里兜底（run 未到尾的异常路径）。
    if (capture_) capture_->stop();
}

void WmScheduler::run() {
    const int kPollMs = 200;

    // ---------- m2: UploadOnly ----------
    if (mode_ == WmMode::UploadOnly) {
        int enq = 0;
        if (upload_) {
            std::vector<std::string> files = Misc::listFilenames(kWmUploadPath);
            for (const std::string& f : files) {
                upload_->enqueue(std::string(kWmUploadPath) + f);
                ++enq;
            }
        }
        Logger::log(LogLevel::INFO, "[wm] op=start mode=2 enqueued=%d from=%s",
                    enq, kWmUploadPath);
        bool uploadStopped = false;
        int64_t idleStart = 0;
        while (true) {
            bool uploadIdle = (!upload_ || upload_->isIdle());
            if (!uploadIdle && !uploadStopped && upload_->firstEnqueueAgeMs() > uploadTimeoutMs_) {
                Logger::log(LogLevel::WARNING,
                            "[wm] op=timeout upload_age_ms=%lld > %lld, requestShutdown",
                            (long long)upload_->firstEnqueueAgeMs(), (long long)uploadTimeoutMs_);
                uploadStopped = true;
                Power::getInstance()->requestShutdown();
            }
            if (uploadIdle) {
                if (idleStart == 0) idleStart = steadyNowMs();
                if (steadyNowMs() - idleStart >= idleGraceMs_) {
                    Logger::log(LogLevel::INFO, "[wm] op=shutdown reason=idle-grace mode=2");
                    break;
                }
            } else {
                idleStart = 0;
            }
            if (lc_.waitForSignal(kPollMs) != 0) {
                Logger::log(LogLevel::INFO, "[wm] op=shutdown reason=signal mode=2");
                break;
            }
        }
        return;
    }

    // ---------- m0/m1: CaptureOnly / CaptureUpload ----------
    const bool oneShot = (std::getenv("HTC_WM_ONE_SHOT") != nullptr);
    bool masked = false;
    bool uploadStopped = false;
    int64_t idleStart = 0;
    Logger::log(LogLevel::INFO, "[wm] op=start mode=%d oneShot=%d",
                static_cast<int>(mode_), oneShot ? 1 : 0);

    while (true) {
        // 1) trigger -> capture（cm==1 时 trigger 内同步等拍照完再起录影，避免 CH2 争用）
        //
        // PIR 门 = !capture_->isBusy() && !masked。
        //   - capture_->isBusy() 在 cm==1 下覆盖 snap+record 整体（snap 期间由
        //     CaptureLane 自身 atomic 标记，record 期间由 RecordTask 自报）——
        //     PIR 撞进行中的原子动作时直接屏蔽。
        //   - masked 仅 oneShot 模式生效（用户显式 opt-in：只触发一次就走完程序）。
        //   - 两条独立：oneShot=1 时首次触发后 masked=true，但 record 完成后
        //     isBusy()=false 也回到可触发态；任意一条锁住都不放过 PIR。
        if (trigger_ && !masked && !(capture_ && capture_->isBusy())
            && trigger_->waitForTrigger(kPollMs) == 1) {
            bool accepted = true;
            if (capture_) accepted = capture_->trigger();
            if (oneShot) masked = true;   // one-shot：首次触发后立即屏蔽——用户 opt-in 的
                                          // 「首拍后即收尾」语义，避开后续 cm==1 撞 IMP wedge
            if (oneShot && mode_ == WmMode::CaptureOnly) {
                Logger::log(LogLevel::INFO,
                            "[wm] op=shutdown reason=one-shot-capture mode=%d accepted=%d",
                            static_cast<int>(mode_), accepted ? 1 : 0);
                break;
            }
            idleStart = 0;   // 捕获活动：重置 idle-grace（sync SnapTask 的 isBusy 恒 false，
                             // 不会经 else 分支重置；显式重置确保 grace 从上次捕获起算）
        }

        // 2) capture idle 状态（idle-grace 判定用）
        const bool captureIdle = (!capture_ || !capture_->isBusy());

        // 3) upload（m1）：idle / timeout。UploadWorker 懒连接——首个 desc（首个 capture
        //    产物）入队才真连，自然满足「Upload 首个 Capture 完成后才启动」。
        bool uploadIdle = (mode_ == WmMode::CaptureOnly) || (!upload_) || upload_->isIdle();
        if (mode_ == WmMode::CaptureUpload && !uploadIdle && !uploadStopped &&
            upload_->firstEnqueueAgeMs() > uploadTimeoutMs_) {
            Logger::log(LogLevel::WARNING,
                        "[wm] op=timeout upload_age_ms=%lld > %lld, requestShutdown",
                        (long long)upload_->firstEnqueueAgeMs(), (long long)uploadTimeoutMs_);
            uploadStopped = true;
            Power::getInstance()->requestShutdown();
        }

        // 4) all-empty 持续 idleGraceMs -> Shutdown
        const bool allEmpty = captureIdle && (uploadIdle || uploadStopped);
        if (allEmpty) {
            if (idleStart == 0) idleStart = steadyNowMs();
            if (steadyNowMs() - idleStart >= idleGraceMs_) {
                Logger::log(LogLevel::INFO, "[wm] op=shutdown reason=idle-grace mode=%d",
                            static_cast<int>(mode_));
                break;
            }
        } else {
            idleStart = 0;
        }

        // 5) 外部信号（SIGTERM / MCU override 经 Power::requestShutdown）-> Shutdown。
        //    非阻塞（trigger waitForTrigger 已等 kPollMs）；首个信号时跑 cleanupHook。
        if (lc_.waitForSignal(0) != 0) {
            Logger::log(LogLevel::INFO, "[wm] op=shutdown reason=signal mode=%d",
                        static_cast<int>(mode_));
            break;
        }
    }

    // 收尾：停 capture（record stop + 通道级释放），再 reset 防析构双停。
    // wm_app 尾随后做 lc.shutdown（IMP teardown）——故 capture 必须先停。
    if (capture_) {
        capture_->stop();
        capture_.reset();
    }
}

}  // namespace app_workmode
