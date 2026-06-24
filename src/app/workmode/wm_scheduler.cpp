// WmScheduler — see wm_scheduler.h. 3 modes: m0(CaptureOnly)/m1(CaptureUpload)/m2(UploadOnly).

#include "wm_scheduler.h"

#include <chrono>
#include <cstdlib>
#include <string>

#include "ProcessLifecycle.h"   // app_lifecycle::ProcessLifecycle
#include "upload_worker.h"      // UploadWorker
#include "capture_lane.h"       // CaptureLane
#include "pir_trigger.h"        // IPirTrigger
#include "misc/Misc.h"          // Misc::listFilenames
// Common.h MUST precede app.h: app.h opens `extern "C"` then #includes Common.h
// inside it — if Common.h isn't already parsed, its templates land under C linkage.
#include "Common.h"
#include "app.h"                // MEDIA_UPLOAD_PATH
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
            std::vector<std::string> files = Misc::listFilenames(MEDIA_UPLOAD_PATH);
            for (const std::string& f : files) {
                upload_->enqueue(std::string(MEDIA_UPLOAD_PATH) + f);
                ++enq;
            }
        }
        Logger::log(LogLevel::INFO, "[wm] op=start mode=2 enqueued=%d from=%s",
                    enq, MEDIA_UPLOAD_PATH);
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
    bool captureTriggered = false;
    bool uploadStopped = false;
    int64_t idleStart = 0;
    Logger::log(LogLevel::INFO, "[wm] op=start mode=%d oneShot=%d",
                static_cast<int>(mode_), oneShot ? 1 : 0);

    while (true) {
        // 1) trigger -> capture（cm==1 时 trigger 内同步等拍照完再起录影，避免 CH2 争用）
        if (trigger_ && !masked && trigger_->waitForTrigger(kPollMs) == 1) {
            if (capture_) capture_->trigger();
            captureTriggered = true;
            idleStart = 0;   // 捕获活动：重置 idle-grace（sync SnapTask 的 isBusy 恒 false，
                             // 不会经 else 分支重置；显式重置确保 grace 从上次捕获起算）
        }

        // 2) one-shot：首个 capture 完成后屏蔽触发
        const bool captureIdle = (!capture_ || !capture_->isBusy());
        if (oneShot && captureTriggered && captureIdle) masked = true;

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
