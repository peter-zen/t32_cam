#ifndef MCU_SERVICE_H
#define MCU_SERVICE_H

#include <atomic>
#include <string>
#include <thread>

#include "McuCache.h"

namespace service {

// Process-internal singleton facade over the hardware MCU class. In test
// mode the optional background polling thread (see startPolling/stopPolling)
// keeps an McuCache warm so query methods never block on I2C. In work mode
// and in PC simulation no thread is started and queries fall through to a
// synchronous MCU::getInstance()->readXxx() call (the underlying I2C bus
// is bypassed at build time in sim).
class McuService {
public:
    static McuService& getInstance();

    // Idempotent. Caller is responsible for only calling this in long-lived
    // modes (htc_main_app with CMD_MOBILE). In work / sim mode leave it
    // un-called so query methods stay synchronous.
    void startPolling(int periodMs);

    // Idempotent. Joins the polling thread if running. Safe to call from
    // main's exit path even if startPolling was never invoked.
    void stopPolling();

    // ---- Read-side query API (identical surface in all modes) -----------
    int getBattery1Voltage() const;
    int getBattery2Voltage() const;
    int getBatteryType() const;
    int getBatteryLevel() const;
    int getExternalVoltage() const;
    int getCds() const;
    int getTemperature() const;
    int getHumidity() const;
    int getAtmosPressure() const;
    int getSignalCF() const;
    int getSignalRSSI() const;
    int getSignalRSRP() const;
    int getSignalRSRQ() const;
    int getSignalSNR() const;
    int getSignalTD() const;
    int getSignalTP() const;
    int getSignalRL() const;
    int getWorkingMode() const;
    std::string getFirmwareVersion() const;
    std::string getMcuFirmwareVersion() const;
    std::string getPID() const;
    std::string getGps() const;
    std::string getSignalType() const;
    std::string getDatetime() const;

    // ---- Mutator --------------------------------------------------------
    // Parse ISO-8601 ("YYYY-MM-DDTHH:MM:SS"), push to system clock via
    // settimeofday(), and forward to MCU::setDatetime. Returns false on
    // parse failure.
    bool setDatetime(const std::string& iso8601);

    // ---- Test seams -----------------------------------------------------
    McuCache& cacheForTest() { return cache_; }
    bool isPolling() const { return running_.load(); }

private:
    McuService();
    ~McuService();
    McuService(const McuService&) = delete;
    McuService& operator=(const McuService&) = delete;

    void pollingLoop(int periodMs);

    McuCache cache_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
};

} // namespace service

#endif // MCU_SERVICE_H
