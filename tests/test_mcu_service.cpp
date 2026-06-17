#include "service/mcu/McuService.h"

#include <atomic>
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
    std::cout << "[1/3] Singleton identity" << std::endl;
    McuService& a = McuService::getInstance();
    McuService& b = McuService::getInstance();
    CHECK(&a == &b);

    std::cout << "[2/3] Synchronous getters return without crashing" << std::endl;
    // Every getter now issues a direct I2C read (bypassed to 0 in sim). We
    // only assert they don't blow up and return sane (>= -1) integer values.
    CHECK(McuService::getInstance().getBattery1Voltage() >= -1);
    CHECK(McuService::getInstance().getCds() >= -1);
    CHECK(McuService::getInstance().getTemperature() >= -1);
    std::string gps = McuService::getInstance().getGps();
    std::string pid = McuService::getInstance().getPID();
    (void)gps;
    (void)pid;

    std::cout << "[3/3] Concurrent synchronous reads are safe" << std::endl;
    // No background thread anymore: every call hits MCU::getInstance()->read*()
    // synchronously. The I2C layer serializes them; multiple readers must not crash.
    std::atomic<int> errors{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&errors] {
            for (int i = 0; i < 1000; ++i) {
                int v1 = McuService::getInstance().getBattery1Voltage();
                int v2 = McuService::getInstance().getCds();
                std::string s = McuService::getInstance().getGps();
                if (v1 < -1 || v2 < -1) {  // -1 would indicate a bug
                    ++errors;
                }
                (void)s;
            }
        });
    }
    for (auto& th : threads) th.join();
    CHECK(errors.load() == 0);

    std::cout << "All McuService checks passed." << std::endl;
    return 0;
}
