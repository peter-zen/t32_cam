// CaptureLane — see capture_lane.h.

#include "capture_lane.h"

#include <atomic>
#include <cstdlib>
#include "Settings.h"   // cameraMode
#include "SharedVideo.h"  // media::resetSharedVideo (HTC_CM1_RESET)
#include "Logger.h"

namespace app_workmode {

CaptureLane::CaptureLane(std::shared_ptr<UploadWorker> uploadWorker)
    : snap_busy_(false) {
    snap_   = std::make_shared<SnapTask>(uploadWorker);
    record_ = std::make_shared<RecordTask>(uploadWorker);
}

bool CaptureLane::trigger() {
    uint8_t cm = Settings::getInstance()->cameraMode;
    if (const char* e = std::getenv("HTC_WM_CAMERA_MODE")) {  // devtest 覆盖（免改 setting.json）
        int v = std::atoi(e);
        if (v >= 0 && v <= 3) cm = static_cast<uint8_t>(v);
    }
    Logger::log(LogLevel::INFO, "CaptureLane: trigger cameraMode=%d", (int)cm);

    // cm==1 原子：snap 期间标记 busy（即便 SnapTask 本身是同步的，lane 仍记录
    // 这个状态供 isBusy() 上报，wm_scheduler 据此屏蔽并发 PIR）。RAII 保证
    // 任意 return 路径（含异常）下 snap_busy_ 都被清掉。
    struct SnapBusyGuard {
        std::atomic<bool>& flag;
        ~SnapBusyGuard() { flag.store(false); }
    } _guard{snap_busy_};
    snap_busy_.store(true);

    if (cm == 0) {            // 仅拍照
        bool ok = snap_->trigger();
        Logger::log(LogLevel::INFO, "CaptureLane: snap trigger complete ok=%d", ok ? 1 : 0);
        return ok;
    }
    if (cm == 2) {            // 仅录影（record_ 是异步，此处 return 不代表录完）
        return record_->trigger();
    }
    if (cm == 1) {            // 拍照 + 录影（顺序：先拍完再录，避免 CH2 争用）
        bool snapOk = snap_->trigger();     // 同步：返回时拍照已完成
        Logger::log(LogLevel::INFO, "CaptureLane: snap trigger complete ok=%d", snapOk ? 1 : 0);
        // HTC_CM1_RESET=1: 在 photo/record 之间做完整 IMP reset（resetSharedVideo →
        // IMP_System_Exit → record 时 re-init），逼近「分开 app」的 fresh-session-per-
        // capture，规避同 session 里 photo 状态残留导致的 wedge。默认关。
        if (const char *e = std::getenv("HTC_CM1_RESET")) {
            if (e[0] == '1') {
                Logger::log(LogLevel::INFO, "CaptureLane: HTC_CM1_RESET — resetSharedVideo between photo and record");
                media::resetSharedVideo();
            }
        }
        return record_->trigger();
    }
    // cm==3（并发拍录）不在 wm 范围（spec §5.1，CH2 8M 约束）。
    Logger::log(LogLevel::WARNING, "CaptureLane: cameraMode=%d not supported (3=concurrent, out of wm scope)", (int)cm);
    return false;
}

bool CaptureLane::isBusy() const {
    // cm==1 原子语义：snap+record 整体在途都视为 busy。
    //   - snap 自身是同步的（~200ms），但 lane 维度仍记录 snap_busy_，
    //     让 wm_scheduler 的 PIR gate（= !isBusy）严格屏蔽 snap 期间触发。
    //   - record 异步在途（~30s/段）是高频撞 PIR 的真实场景。
    return snap_busy_.load() || (record_ && record_->isRecording());
}

void CaptureLane::stop() {
    // snap 同步无需停；record 要停 + 等 onComplete（通道级释放，不 IMP_System_Exit）。
    if (record_) record_->stop();
}

}  // namespace app_workmode
