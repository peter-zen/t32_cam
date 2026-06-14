#ifndef MCU_CACHE_H
#define MCU_CACHE_H

#include <atomic>
#include <mutex>
#include <string>

namespace service {

// Thread-safe cache populated by McuService's polling thread and read by
// query methods on arbitrary threads. POD ints use std::atomic; strings are
// guarded by a single mutex so the polling writer and HTTP-handler readers
// never collide.
struct McuCache {
    // ----- POD integer fields (lock-free atomic) ------------------------
    std::atomic<int> battery1Voltage{0};
    std::atomic<int> battery2Voltage{0};
    std::atomic<int> batteryType{0};
    std::atomic<int> batteryLevel{0};
    std::atomic<int> externalVoltage{0};
    std::atomic<int> cds{0};
    std::atomic<int> temperature{0};
    std::atomic<int> humidity{0};
    std::atomic<int> pressure{0};
    std::atomic<int> signalCF{0};
    std::atomic<int> signalRSSI{0};
    std::atomic<int> signalRSRP{0};
    std::atomic<int> signalRSRQ{0};
    std::atomic<int> signalSNR{0};
    std::atomic<int> signalTD{0};
    std::atomic<int> signalTP{0};
    std::atomic<int> signalRL{0};
    std::atomic<int> workingMode{0};

    // Wall-clock millis of the last successful polling tick (or 0).
    std::atomic<long long> lastUpdatedMs{0};

    // ----- String fields (mutex protected) ------------------------------
    mutable std::mutex stringMutex;
    std::string firmwareVersion;
    std::string mcuVersion;
    std::string pid;
    std::string gps;
    std::string signalType;

    std::string getFirmwareVersion() const {
        std::lock_guard<std::mutex> lk(stringMutex);
        return firmwareVersion;
    }
    std::string getMcuVersion() const {
        std::lock_guard<std::mutex> lk(stringMutex);
        return mcuVersion;
    }
    std::string getPid() const {
        std::lock_guard<std::mutex> lk(stringMutex);
        return pid;
    }
    std::string getGps() const {
        std::lock_guard<std::mutex> lk(stringMutex);
        return gps;
    }
    std::string getSignalType() const {
        std::lock_guard<std::mutex> lk(stringMutex);
        return signalType;
    }

    void setFirmwareVersion(std::string v) {
        std::lock_guard<std::mutex> lk(stringMutex);
        firmwareVersion = std::move(v);
    }
    void setMcuVersion(std::string v) {
        std::lock_guard<std::mutex> lk(stringMutex);
        mcuVersion = std::move(v);
    }
    void setPid(std::string v) {
        std::lock_guard<std::mutex> lk(stringMutex);
        pid = std::move(v);
    }
    void setGps(std::string v) {
        std::lock_guard<std::mutex> lk(stringMutex);
        gps = std::move(v);
    }
    void setSignalType(std::string v) {
        std::lock_guard<std::mutex> lk(stringMutex);
        signalType = std::move(v);
    }
};

} // namespace service

#endif // MCU_CACHE_H
