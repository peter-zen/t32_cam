#include "McuService.h"

#include <sys/time.h>
#include <time.h>

#include <string>

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
    elog_i(kTag, "McuService destroyed");
}

// --- Query API ------------------------------------------------------------
// Synchronous: each getter issues the I2C read directly via MCU. The I2C
// layer serializes access with its own mutex, so concurrent calls from
// multiple HTTP worker threads are safe but will queue on the bus.
int McuService::getBattery1Voltage() const {
    return MCU::getInstance()->readBattery1Voltage();
}
int McuService::getBattery2Voltage() const {
    return MCU::getInstance()->readBattery2Voltage();
}
int McuService::getBatteryType() const {
    return MCU::getInstance()->readBatteryType();
}
int McuService::getBatteryLevel() const {
    return MCU::getInstance()->readBatteryLevel();
}
int McuService::getExternalVoltage() const {
    return MCU::getInstance()->readExternalVoltage();
}
int McuService::getCds() const {
    return MCU::getInstance()->readCds();
}
int McuService::getTemperature() const {
    return MCU::getInstance()->readTemperature();
}
int McuService::getHumidity() const {
    return MCU::getInstance()->readHumidity();
}
int McuService::getAtmosPressure() const {
    return MCU::getInstance()->readAtmosPressure();
}
int McuService::getSignalCF() const {
    return MCU::getInstance()->readSignalCF();
}
int McuService::getSignalRSSI() const {
    return MCU::getInstance()->readSignalRSSI();
}
int McuService::getSignalRSRP() const {
    return MCU::getInstance()->readSignalRSRP();
}
int McuService::getSignalRSRQ() const {
    return MCU::getInstance()->readSignalRSRQ();
}
int McuService::getSignalSNR() const {
    return MCU::getInstance()->readSignalSNR();
}
int McuService::getSignalTD() const {
    return MCU::getInstance()->readSignalTD();
}
int McuService::getSignalTP() const {
    return MCU::getInstance()->readSignalTP();
}
int McuService::getSignalRL() const {
    return MCU::getInstance()->readSignalRL();
}
int McuService::getWorkingMode() const {
    return MCU::getInstance()->readWorkingMode();
}
std::string McuService::getFirmwareVersion() const {
    return MCU::getInstance()->readFirmwareVersion();
}
std::string McuService::getMcuFirmwareVersion() const {
    auto m = MCU::getInstance();
    return m->convertVersion(m->readVersion());
}
std::string McuService::getPID() const {
    return MCU::getInstance()->readPID();
}
std::string McuService::getGps() const {
    return MCU::getInstance()->readGps();
}
std::string McuService::getSignalType() const {
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
