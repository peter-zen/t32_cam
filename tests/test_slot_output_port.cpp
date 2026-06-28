// SlotOutputPort unit test — locks the signal-port state machine that backs the
// wm upload Done condition (isSettled = idle && no pending token, atomic under the
// port mutex). See doc/knowledge/specs/wm-task-slot-scheduler.md §1.1/§1.2.
//
// Deterministic by design: state checks are lock-guarded; the wake/stop cases wait
// (bounded) for the consumer thread to reach the expected parked state.

#include "slot_output_port.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using app_workmode::SlotOutputPort;

namespace {

void require(bool cond, const std::string& message) {
    if (!cond) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

// 等待 predicate 变真（bounded busy-wait，避免测试 hang）。
template <typename Pred>
void waitUntil(Pred pred, int timeoutMs, const char* /*ctx*/) {
    for (int i = 0; i < timeoutMs / 2 && !pred(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

void test_initial_and_push_state() {
    SlotOutputPort p;
    // 初始：消费者未 park（idle=false）→ busy、未 settled、无 token。
    require(p.isBusy(), "fresh port: consumer not parked -> busy");
    require(!p.isSettled(), "fresh port: not settled");
    require(!p.hasPending(), "fresh port: no pending token");

    p.push();
    require(p.hasPending(), "after push: has pending token");
    require(!p.isSettled(), "pending token -> not settled");

    p.push();
    require(p.hasPending(), "second push still pending");
}

// 消费者 park → 被 push 唤醒 → 消费 token → 重新 park → isSettled 为真。
void test_consumer_park_wake_settle() {
    SlotOutputPort p;
    std::atomic<int> wakes{0};
    std::atomic<bool> running{true};

    std::thread consumer([&] {
        while (running.load()) {
            p.setBusy();
            if (!p.parkIfNoWork()) break;  // stopped
            wakes.fetch_add(1);
        }
    });

    // 等 consumer 首次进入 park。
    waitUntil([&] { return p.isSettled(); }, 500, "first park");
    require(p.isSettled(), "consumer parked initially -> settled");

    p.push();  // 唤醒
    // consumer 消费 token 后会再 park → 再次 settled。
    waitUntil([&] { return p.isSettled() && wakes.load() >= 1; }, 500, "after wake");
    require(wakes.load() >= 1, "consumer woke on push");
    require(p.isSettled(), "after wake + re-park: settled (idle, no token)");
    require(!p.hasPending(), "consumed token: no pending");

    running.store(false);
    p.signalStop();
    consumer.join();
}

// signalStop 必须唤醒 park 中的消费者并令 parkIfNoWork 返回 false。
void test_signal_stop_wakes_parked() {
    SlotOutputPort p;
    std::atomic<bool> returnedFalse{false};

    std::thread consumer([&] {
        p.setBusy();
        bool r = p.parkIfNoWork();
        returnedFalse.store(!r);
    });

    waitUntil([&] { return p.isSettled(); }, 500, "park before stop");
    require(p.isSettled(), "consumer parked -> settled");

    p.signalStop();
    consumer.join();
    require(returnedFalse.load(), "signalStop made parkIfNoWork return false");
}

}  // namespace

int main() {
    test_initial_and_push_state();
    test_consumer_park_wake_settle();
    test_signal_stop_wakes_parked();
    std::cout << "test_slot_output_port: all tests passed" << std::endl;
    return 0;
}
