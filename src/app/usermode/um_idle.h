#pragma once

#include <cstdint>
#include <mutex>

namespace app_usermode {

// um idle-timeout 自关机的关机原因（spec um-app-spec §4）。
enum class UmShutdownReason {
    None = 0,
    IdleTimeout,
};

// um idle-timeout 配置（spec §7）。产品值由 env HTC_UM_IDLE_TIMEOUT_MS /
// HTC_UM_HTTP_ACTIVITY_WINDOW_MS 注入；L1 sim 测试用注入小值。
struct UmIdleConfig {
    int64_t idleTimeoutMs = 300000;       // 持续无活跃达此值 → IdleTimeout（spec §4）
    int64_t httpActivityWindowMs = 5000;  // HTTP 请求"活跃窗口"（spec §4）
};

// UmIdleCore — um idle-timeout 自关机的纯决策内核（spec §3 server-lifecycle 的
// idle-wait 阶段 + §4 关机触发）。
//
// HAL-free：时间由 tick(nowMs) 注入，事件更新内部状态，绝不调用 chrono::now / sleep /
// RtspServer / http_server / Power。只标记 shouldShutdown()；真实关机由 wiring 层
// (um_app.cpp) 在 shouldShutdown()==true 时调 Power::getInstance()->requestShutdown() ——
// 后者 kill(SIGTERM) 自身，自动走 wm 同款 ProcessLifecycle stop-condition → teardown →
// Misc::poweroff()，故 idle-timeout 与 ctrl-c 共用同一条 teardown 路径（spec §6 决策10、
// §11.1）。与 wm WmTaskSchedulerCore 的"纯核心只标记、重 wrapper 触发"两层拆分同构。
//
// 活跃定义（spec §4 决策5）：RTSP 客户端连接中(count>0) OR HTTP 请求在活跃窗口内。
// mDNS 不算活跃（主动广播，客户端"发现到" ≠ "在用"）。
class UmIdleCore {
public:
    explicit UmIdleCore(const UmIdleConfig& config);

    // 活跃事件（由 wiring 层在真实 RTSP 连接 / HTTP 请求发生时调用）。
    void onRtspConnect(int64_t nowMs);     // RTSP 客户端连入（count+1，续命）
    void onRtspDisconnect(int64_t nowMs);  // RTSP 客户端断开（count-1）
    void onHttpRequest(int64_t nowMs);     // 收到 HTTP 请求（刷新 lastHttp 时间戳，续命）

    // 周期驱动（对称 wm WmTaskSchedulerCore::tick）：查活跃态，达 idleTimeout 标记关机。
    void tick(int64_t nowMs);

    bool shouldShutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return reason_ != UmShutdownReason::None;
    }
    UmShutdownReason shutdownReason() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return reason_;
    }

private:
    bool isActive(int64_t nowMs) const;  // 调用者持锁（仅 tick 内部调用）

    UmIdleConfig config_;
    int rtspClientCount_ = 0;
    int64_t lastHttpActivityMs_ = 0;  // 0=从未收到 HTTP 请求
    int64_t idleStartMs_ = 0;         // 0=当前活跃或尚未进入 idle
    UmShutdownReason reason_ = UmShutdownReason::None;
    mutable std::mutex mutex_;        // 事件 handler(rtsp/http 线程)与 tick(main 线程)共享
};

}  // namespace app_usermode
