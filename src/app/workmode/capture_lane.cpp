// CaptureLane — see capture_lane.h.

#include "capture_lane.h"

#include <cstdlib>
#include "Settings.h"   // cameraMode
#include "SharedVideo.h"  // media::resetSharedVideo (HTC_CM1_RESET)
#include "Logger.h"

namespace app_workmode {

CaptureLane::CaptureLane(std::shared_ptr<UploadWorker> uploadWorker) {
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

    if (cm == 0) {            // 仅拍照
        return snap_->trigger();
    }
    if (cm == 2) {            // 仅录影
        return record_->trigger();
    }
    if (cm == 1) {            // 拍照 + 录影（顺序：先拍完再录，避免 CH2 争用）
        snap_->trigger();     // 同步：返回时拍照已完成
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
    // snap 同步 ⇒ isBusy 恒 false；只有 record 异步在途时 busy。
    return record_ && record_->isRecording();
}

void CaptureLane::stop() {
    // snap 同步无需停；record 要停 + 等 onComplete（通道级释放，不 IMP_System_Exit）。
    if (record_) record_->stop();
}

}  // namespace app_workmode
