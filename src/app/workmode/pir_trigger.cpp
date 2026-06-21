#include "pir_trigger.h"

#include "Logger.h"

#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>

namespace app_workmode {

// ===================== SimPirTrigger =====================

SimPirTrigger::SimPirTrigger(int intervalMs) : intervalMs_(intervalMs) {
    efd_ = eventfd(0, 0);
    if (efd_ < 0) {
        Logger::log(LogLevel::ERROR, "SimPirTrigger: eventfd failed");
        return;
    }
    if (intervalMs_ > 0) {
        run_ = true;
        timer_ = std::thread([this] {
            while (run_.load()) {
                // 分段 sleep（100ms 粒度）以便及时响应 stop
                for (int acc = 0; acc < intervalMs_ && run_.load(); acc += 100) {
                    int step = (intervalMs_ - acc < 100) ? (intervalMs_ - acc) : 100;
                    std::this_thread::sleep_for(std::chrono::milliseconds(step));
                }
                if (!run_.load()) break;
                uint64_t one = 1;
                if (write(efd_, &one, sizeof(one)) < 0) break;
            }
        });
    }
    Logger::log(LogLevel::INFO, "SimPirTrigger: started, interval=%dms", intervalMs_);
}

SimPirTrigger::~SimPirTrigger() {
    run_ = false;
    if (timer_.joinable()) timer_.join();
    if (efd_ >= 0) close(efd_);
}

int SimPirTrigger::waitForTrigger(int timeoutMs) {
    if (efd_ < 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        return 0;
    }
    struct pollfd pfd;
    pfd.fd = efd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ret = poll(&pfd, 1, timeoutMs);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        uint64_t val = 0;
        read(efd_, &val, sizeof(val));
        return 1;
    }
    return 0;
}

void SimPirTrigger::inject() {
    if (efd_ >= 0) {
        uint64_t one = 1;
        write(efd_, &one, sizeof(one));
    }
}

// ===================== GpioPirTrigger（预留） =====================

GpioPirTrigger::GpioPirTrigger(int gpioPin) : gpioPin_(gpioPin) {
    // TODO 硬件改版后：
    //   1) GPIO export + setDirection(INPUT) + setEdge(GPIO_EDGE_BOTH)
    //      （GPIO.h:104 / IngenicGpio.cpp:51-58 写 /sys/class/gpio/gpioN/edge）
    //   2) valueFd_ = open("/sys/class/gpio/gpioN/value", O_RDONLY)
    //   3) 先 read 一次清 pending
    // 当前 PIR GPIO pin 未确定，暂不接线。
    Logger::log(LogLevel::WARNING, "GpioPirTrigger: pin %d not wired yet (hardware TBD)", gpioPin_);
}

GpioPirTrigger::~GpioPirTrigger() {
    if (valueFd_ >= 0) close(valueFd_);
}

int GpioPirTrigger::waitForTrigger(int timeoutMs) {
    // TODO: poll(valueFd_, POLLPRI, timeoutMs) → lseek(0,SEEK_SET) → read value
    // 当前未实现，总是 timeout（占位，硬件改版后补全，循环结构不变）。
    std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
    return 0;
}

}  // namespace app_workmode
