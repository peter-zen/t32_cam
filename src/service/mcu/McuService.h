#ifndef MCU_SERVICE_H
#define MCU_SERVICE_H

#include <string>

namespace service {

// Process-internal singleton facade over the hardware MCU class. Query methods
// are synchronous: every getter issues the underlying I2C read directly via
// MCU::getInstance()->readXxx(). There is no background polling thread and no
// cache — the freshness/cadence of MCU data is driven entirely by whoever
// calls these getters. In the -m app that is the HTTP request handlers, so the
// connected app decides how often data is refreshed (by how often it polls the
// HTTP endpoints). In PC simulation the I2C bus is bypassed at build time and
// reads return 0.
class McuService {
public:
    static McuService& getInstance();

    // ---- Read-side query API (synchronous, on-demand) -----------------
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

private:
    McuService();
    ~McuService();
    McuService(const McuService&) = delete;
    McuService& operator=(const McuService&) = delete;
};

} // namespace service

#endif // MCU_SERVICE_H
