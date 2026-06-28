#pragma once

#include <atomic>
#include <thread>

namespace app_workmode {

// PIR 触发源抽象（阶段3）。事件循环通过 waitForTrigger 等待一次 PIR 触发。
// 返回 1=触发, 0=超时。
class IPirTrigger {
public:
    virtual ~IPirTrigger() = default;
    virtual int waitForTrigger(int timeoutMs) = 0;
};

// 模拟 PIR（测试用，sim + 当前 T32）：eventfd + 定时线程按 intervalMs 注入假触发。
// intervalMs <= 0 时不自动注入（靠 inject() 手动，供将来接信号/HTTP 注入）。
class SimPirTrigger : public IPirTrigger {
public:
    explicit SimPirTrigger(int intervalMs, int maxCount = 0);  // maxCount<=0 = 无限触发
    ~SimPirTrigger() override;
    int waitForTrigger(int timeoutMs) override;
    void inject();  // 手动注入一次触发（测试用）
private:
    int efd_ = -1;
    int intervalMs_;
    int maxCount_;            // <=0 = 无限触发；>0 = 触发 N 次后停（建模「动物离开」）
    std::atomic<bool> run_{false};
    std::thread timer_;
};

// GPIO PIR（预留，硬件改版后接）：GPIO 边沿中断 + poll(/sys/class/gpio/gpioN/value)。
// gpioPin 待硬件确定，当前 waitForTrigger 总是 timeout（未接线，仅占位）。
class GpioPirTrigger : public IPirTrigger {
public:
    explicit GpioPirTrigger(int gpioPin);
    ~GpioPirTrigger() override;
    int waitForTrigger(int timeoutMs) override;
private:
    int gpioPin_;
    int valueFd_ = -1;
};

}  // namespace app_workmode
