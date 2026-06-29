#include "um_idle.h"

namespace app_usermode {

UmIdleCore::UmIdleCore(const UmIdleConfig& config) : config_(config) {}

void UmIdleCore::onRtspConnect(int64_t /*nowMs*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++rtspClientCount_;
    idleStartMs_ = 0;  // 活跃 → 续命（与 wm onExternalCaptureTrigger 的 idleStartMs_=0 同构）
}

void UmIdleCore::onRtspDisconnect(int64_t /*nowMs*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (rtspClientCount_ > 0) --rtspClientCount_;
    // 断开本身不算活跃，不在此重置 idleStartMs_；由 tick 查 count==0 进入 idle 计时。
}

void UmIdleCore::onHttpRequest(int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    lastHttpActivityMs_ = nowMs;
    idleStartMs_ = 0;  // 活跃 → 续命
}

bool UmIdleCore::isActive(int64_t nowMs) const {
    if (rtspClientCount_ > 0) return true;
    // lastHttpActivityMs_==0 表示从未收到请求，不能误判为活跃。
    if (lastHttpActivityMs_ > 0 && nowMs - lastHttpActivityMs_ < config_.httpActivityWindowMs)
        return true;
    return false;
}

void UmIdleCore::tick(int64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (reason_ != UmShutdownReason::None) return;  // 幂等：已决定关机就不再变

    if (isActive(nowMs)) {
        idleStartMs_ = 0;  // 活跃 → 续命（兜底，正常情况下事件已归零）
    } else {
        if (idleStartMs_ == 0) idleStartMs_ = nowMs;  // 首次进入 idle 记时
        if (nowMs - idleStartMs_ >= config_.idleTimeoutMs)
            reason_ = UmShutdownReason::IdleTimeout;  // 仅标记，不调 Power（spec §6 决策10）
    }
}

}  // namespace app_usermode
