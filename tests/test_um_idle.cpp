#include "um_idle.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace app_usermode;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

// 注入小超时 + 整数 nowMs，全程无 sleep / 真实时钟（与 test_wm_task_scheduler 同款）。
UmIdleConfig config() {
    UmIdleConfig cfg;
    cfg.idleTimeoutMs = 100;
    cfg.httpActivityWindowMs = 50;
    return cfg;
}

void test_idle_timeout_triggers() {
    UmIdleCore idle(config());
    idle.tick(1000);  // 无活跃 → idleStartMs_=1000
    require(!idle.shouldShutdown(), "no shutdown immediately after entering idle");
    idle.tick(1100);  // 1100-1000=100 >= 100 → IdleTimeout
    require(idle.shouldShutdown(), "sustained idle triggers shutdown");
    require(idle.shutdownReason() == UmShutdownReason::IdleTimeout, "reason is idle-timeout");
}

void test_rtsp_connection_keeps_alive() {
    UmIdleCore idle(config());
    idle.onRtspConnect(1000);  // count=1
    idle.tick(5000);           // 远超 timeout，但 RTSP 连接中
    require(!idle.shouldShutdown(), "rtsp client connection keeps um alive past timeout");
}

void test_http_activity_window() {
    UmIdleCore idle(config());
    idle.onHttpRequest(1000);  // lastHttp=1000, idleStartMs_=0
    idle.tick(1040);           // 1040-1000=40 < 50 → 活跃
    require(!idle.shouldShutdown(), "http request within activity window keeps alive");
    idle.tick(1051);           // 1051-1000=51 >= 50 → 不活跃，进 idle idleStartMs_=1051
    require(!idle.shouldShutdown(), "no shutdown at http window expiry");
    idle.tick(1152);           // 1152-1051=101 >= 100 → IdleTimeout
    require(idle.shouldShutdown(), "shutdown after sustained idle past http window");
}

void test_activity_resets_idle_timer() {
    UmIdleCore idle(config());
    idle.tick(1000);           // idleStartMs_=1000
    idle.tick(1050);           // 50 < 100，仍计时（idleStartMs_ 保持 1000）
    require(!idle.shouldShutdown(), "counting idle, no shutdown yet");
    idle.onHttpRequest(1050);  // 活跃 → idleStartMs_ 归零（重置）
    // 若未重置，idleStartMs_ 仍是 1000 → 下面的 tick(1101) 就会 1101-1000>=100 触发关机
    idle.tick(1101);           // 1101-1050=51 >= 50 → 进 idle，idleStartMs_=1101（新计时）
    require(!idle.shouldShutdown(), "idle timer restarted from activity reset, not shutdown");
    idle.tick(1202);           // 1202-1101=101 >= 100 → IdleTimeout
    require(idle.shouldShutdown(), "shutdown only after freshly counted idle");
}

void test_rtsp_disconnect_then_idle() {
    UmIdleCore idle(config());
    idle.onRtspConnect(1000);
    idle.tick(2000);             // 连接中，alive
    require(!idle.shouldShutdown(), "connected: alive past timeout");
    idle.onRtspDisconnect(3000);  // count=0
    idle.tick(3000);             // 进 idle idleStartMs_=3000
    require(!idle.shouldShutdown(), "no shutdown right after disconnect");
    idle.tick(3101);             // 3101-3000=101 >= 100 → IdleTimeout
    require(idle.shouldShutdown(), "shutdown after sustained idle post-disconnect");
}

void test_no_shutdown_before_timeout() {
    UmIdleCore idle(config());
    idle.tick(1000);  // idleStartMs_=1000
    idle.tick(1099);  // 99 < 100
    require(!idle.shouldShutdown(), "no shutdown strictly before timeout");
    idle.tick(1100);  // 100 >= 100
    require(idle.shouldShutdown(), "shutdown exactly at timeout boundary");
}

void test_multiple_rtsp_clients() {
    UmIdleCore idle(config());
    idle.onRtspConnect(1000);  // count=1
    idle.onRtspConnect(1000);  // count=2
    idle.tick(5000);           // 远超 timeout，2 个连接中
    require(!idle.shouldShutdown(), "two rtsp clients keep alive");
    idle.onRtspDisconnect(6000);  // count=1，仍有连接
    idle.tick(7000);
    require(!idle.shouldShutdown(), "one client remains: still alive after one disconnect");
    idle.onRtspDisconnect(8000);  // count=0
    idle.tick(8000);              // 进 idle idleStartMs_=8000
    require(!idle.shouldShutdown(), "no shutdown right after last disconnect");
    idle.tick(8101);              // 101 >= 100
    require(idle.shouldShutdown(), "shutdown only after all clients disconnect and idle");
}

void test_disconnect_without_connect_does_not_unbalance_count() {
    UmIdleCore idle(config());
    // stray disconnect（未 connect 先 disconnect）若无 guard 会让 count 变 -1，之后一次
    // connect 只能抵回 0 → 误判无连接。guard 把 count 夹在 0，故 connect 后 count=1，alive。
    idle.onRtspDisconnect(1000);  // count 夹在 0（非 -1）
    idle.onRtspConnect(1000);     // count=1
    idle.tick(5000);              // 远超 timeout，但应 alive（count=1）
    require(!idle.shouldShutdown(), "stray disconnect does not unbalance count: one connect still alive");
}

}  // namespace

int main() {
    test_idle_timeout_triggers();
    test_rtsp_connection_keeps_alive();
    test_http_activity_window();
    test_activity_resets_idle_timer();
    test_rtsp_disconnect_then_idle();
    test_no_shutdown_before_timeout();
    test_multiple_rtsp_clients();
    test_disconnect_without_connect_does_not_unbalance_count();

    std::cout << "test_um_idle: all tests passed" << std::endl;
    return 0;
}
