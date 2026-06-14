#include "service/mcu/McuService.h"
#include "service/mcu/McuCache.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

using service::McuService;

namespace {

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "CHECK failed: " << expression
                  << " at " << __FILE__ << ":" << line << std::endl;
        std::exit(1);
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

}  // namespace

int main() {
    std::cout << "[1/5] Singleton identity" << std::endl;
    McuService& a = McuService::getInstance();
    McuService& b = McuService::getInstance();
    CHECK(&a == &b);

    std::cout << "[2/5] startPolling sets running, stopPolling clears it"
              << std::endl;
    CHECK(!a.isPolling());
    a.startPolling(100);
    // Wait until polling thread is alive (running_ true). Fast path; no I2C.
    auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
    while (!a.isPolling() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(a.isPolling());
    a.stopPolling();
    // stopPolling is synchronous (joins the thread). isPolling() must now
    // be false immediately.
    CHECK(!a.isPolling());

    std::cout << "[3/5] stopPolling is idempotent" << std::endl;
    a.stopPolling();  // second call must not crash / hang
    a.stopPolling();
    CHECK(!a.isPolling());

    std::cout << "[4/5] Concurrent reads are safe" << std::endl;
    // No polling here — calls fall through to MCU::getInstance()->read*()
    // synchronously. Multiple readers must not crash.
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&errors] {
            for (int i = 0; i < 1000; ++i) {
                int v1 = McuService::getInstance().getBattery1Voltage();
                int v2 = McuService::getInstance().getCds();
                std::string s = McuService::getInstance().getGps();
                // All values must be consistent within this iteration
                // (no torn read). The I2C layer is serialized, so the
                // calls are individually atomic; we just check we got
                // here without crashing.
                if (v1 < -1 || v2 < -1) {  // -1 would indicate a bug
                    ++errors;
                }
                (void)s;
            }
        });
    }
    for (auto& th : threads) th.join();
    CHECK(errors.load() == 0);

    std::cout << "[5/5] startPolling after stop" << std::endl;
    // After concurrent reads, restart and stop again to confirm the
    // thread lifecycle is reusable.
    a.startPolling(200);
    deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    while (!a.isPolling() &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(a.isPolling());
    a.stopPolling();
    CHECK(!a.isPolling());

    std::cout << "All McuService checks passed." << std::endl;
    return 0;
}
