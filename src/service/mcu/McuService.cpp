#include "McuService.h"

#include <sys/time.h>
#include <time.h>

#include <chrono>
#include <condition_variable>
#include <mutex>

#include <elog.h>

#include "MCU.h"

namespace service {

namespace {
constexpr const char* kTag = "mcu_svc";
}

McuService& McuService::getInstance() {
    static McuService instance;
    return instance;
}

McuService::McuService() {
    elog_i(kTag, "McuService constructed");
}

McuService::~McuService() {
    if (running_.load()) {
        stopPolling();
    }
    elog_i(kTag, "McuService destroyed");
}

void McuService::startPolling(int periodMs) {
    if (running_.exchange(true)) {
        elog_w(kTag, "startPolling(%d): already running, ignored", periodMs);
        return;
    }
    stopRequested_.store(false);
    elog_i(kTag, "McuService: startPolling(%d)", periodMs);
    thread_ = std::thread(&McuService::pollingLoop, this, periodMs);
}

void McuService::stopPolling() {
    if (!running_.exchange(false)) {
        return;  // already stopped
    }
    stopRequested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    elog_i(kTag, "McuService: stopPolling() joined");
}

void McuService::pollingLoop(int periodMs) {
    elog_i(kTag, "pollingLoop: entered, periodMs=%d", periodMs);
    std::mutex wakeMutex;
    std::condition_variable wake;
    auto next = std::chrono::steady_clock::now();
    while (!stopRequested_.load()) {
        // One polling tick: read everything from MCU once and store into cache.
        auto mcu = MCU::getInstance();
        cache_.battery1Voltage.store(mcu->readBattery1Voltage());
        cache_.battery2Voltage.store(mcu->readBattery2Voltage());
        cache_.batteryType.store(mcu->readBatteryType());
        cache_.batteryLevel.store(mcu->readBatteryLevel());
        cache_.externalVoltage.store(mcu->readExternalVoltage());
        cache_.cds.store(mcu->readCds());
        cache_.temperature.store(mcu->readTemperature());
        cache_.humidity.store(mcu->readHumidity());
        cache_.pressure.store(mcu->readAtmosPressure());
        cache_.signalCF.store(mcu->readSignalCF());
        cache_.signalRSSI.store(mcu->readSignalRSSI());
        cache_.signalRSRP.store(mcu->readSignalRSRP());
        cache_.signalRSRQ.store(mcu->readSignalRSRQ());
        cache_.signalSNR.store(mcu->readSignalSNR());
        cache_.signalTD.store(mcu->readSignalTD());
        cache_.signalTP.store(mcu->readSignalTP());
        cache_.signalRL.store(mcu->readSignalRL());
        cache_.workingMode.store(mcu->readWorkingMode());
        cache_.setFirmwareVersion(mcu->readFirmwareVersion());
        cache_.setMcuVersion(mcu->convertVersion(mcu->readVersion()));
        cache_.setPid(mcu->readPID());
        cache_.setGps(mcu->readGps());
        cache_.setSignalType(mcu->readSignalType());

        using namespace std::chrono;
        cache_.lastUpdatedMs.store(
            duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());

        next += milliseconds(periodMs);
        std::unique_lock<std::mutex> lk(wakeMutex);
        wake.wait_until(lk, next, [this] { return stopRequested_.load(); });
    }
    elog_i(kTag, "pollingLoop: exit");
}

// --- Query API ------------------------------------------------------------
// When polling is active, read from cache. Otherwise call MCU synchronously.
int McuService::getBattery1Voltage() const {
    return running_.load() ? cache_.battery1Voltage.load()
                           : MCU::getInstance()->readBattery1Voltage();
}
int McuService::getBattery2Voltage() const {
    return running_.load() ? cache_.battery2Voltage.load()
                           : MCU::getInstance()->readBattery2Voltage();
}
int McuService::getBatteryType() const {
    return running_.load() ? cache_.batteryType.load()
                           : MCU::getInstance()->readBatteryType();
}
int McuService::getBatteryLevel() const {
    return running_.load() ? cache_.batteryLevel.load()
                           : MCU::getInstance()->readBatteryLevel();
}
int McuService::getExternalVoltage() const {
    return running_.load() ? cache_.externalVoltage.load()
                           : MCU::getInstance()->readExternalVoltage();
}
int McuService::getCds() const {
    return running_.load() ? cache_.cds.load() : MCU::getInstance()->readCds();
}
int McuService::getTemperature() const {
    return running_.load() ? cache_.temperature.load()
                           : MCU::getInstance()->readTemperature();
}
int McuService::getHumidity() const {
    return running_.load() ? cache_.humidity.load()
                           : MCU::getInstance()->readHumidity();
}
int McuService::getAtmosPressure() const {
    return running_.load() ? cache_.pressure.load()
                           : MCU::getInstance()->readAtmosPressure();
}
int McuService::getSignalCF() const {
    return running_.load() ? cache_.signalCF.load()
                           : MCU::getInstance()->readSignalCF();
}
int McuService::getSignalRSSI() const {
    return running_.load() ? cache_.signalRSSI.load()
                           : MCU::getInstance()->readSignalRSSI();
}
int McuService::getSignalRSRP() const {
    return running_.load() ? cache_.signalRSRP.load()
                           : MCU::getInstance()->readSignalRSRP();
}
int McuService::getSignalRSRQ() const {
    return running_.load() ? cache_.signalRSRQ.load()
                           : MCU::getInstance()->readSignalRSRQ();
}
int McuService::getSignalSNR() const {
    return running_.load() ? cache_.signalSNR.load()
                           : MCU::getInstance()->readSignalSNR();
}
int McuService::getSignalTD() const {
    return running_.load() ? cache_.signalTD.load()
                           : MCU::getInstance()->readSignalTD();
}
int McuService::getSignalTP() const {
    return running_.load() ? cache_.signalTP.load()
                           : MCU::getInstance()->readSignalTP();
}
int McuService::getSignalRL() const {
    return running_.load() ? cache_.signalRL.load()
                           : MCU::getInstance()->readSignalRL();
}
int McuService::getWorkingMode() const {
    return running_.load() ? cache_.workingMode.load()
                           : MCU::getInstance()->readWorkingMode();
}
std::string McuService::getFirmwareVersion() const {
    if (running_.load()) return cache_.getFirmwareVersion();
    return MCU::getInstance()->readFirmwareVersion();
}
std::string McuService::getMcuFirmwareVersion() const {
    if (running_.load()) return cache_.getMcuVersion();
    auto m = MCU::getInstance();
    return m->convertVersion(m->readVersion());
}
std::string McuService::getPID() const {
    if (running_.load()) return cache_.getPid();
    return MCU::getInstance()->readPID();
}
std::string McuService::getGps() const {
    if (running_.load()) return cache_.getGps();
    return MCU::getInstance()->readGps();
}
std::string McuService::getSignalType() const {
    if (running_.load()) return cache_.getSignalType();
    return MCU::getInstance()->readSignalType();
}
std::string McuService::getDatetime() const {
    struct tm t = MCU::getInstance()->getDatetime();
    char buf[32];
    if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &t) == 0) {
        return std::string();
    }
    return std::string(buf);
}

bool McuService::setDatetime(const std::string& iso8601) {
    struct tm t{};
    if (strptime(iso8601.c_str(), "%Y-%m-%dT%H:%M:%S", &t) == nullptr) {
        elog_w(kTag, "setDatetime: failed to parse '%s'", iso8601.c_str());
        return false;
    }
    time_t utc = timegm(&t);
    struct timeval tv{};
    tv.tv_sec = utc;
    tv.tv_usec = 0;
    if (settimeofday(&tv, nullptr) != 0) {
        elog_w(kTag, "setDatetime: settimeofday failed (ok if not root)");
    }
    return MCU::getInstance()->setDatetime(&t);
}

} // namespace service
