#ifndef SERVICE_TYPES_H
#define SERVICE_TYPES_H

#include <string>

namespace service {

struct DeviceInfo {
    std::string pid;
    std::string firmwareVersion;
    std::string model;
    std::string buildDate;
    std::string mcuVersion;
};

struct SensorData {
    int batteryVoltage = 0;
    int batteryType = 0;
    int batteryLevel = 0;
    int externalVoltage = 0;
    int sdcardCapacity = 0;
    int sdcardUsed = 0;
    int cds = 0;
    int temperature = 0;
    int pressure = 0;
    int humidity = 0;
    std::string datetime;
};

struct StorageInfo {
    int total = 0;
    int free = 0;
    int used = 0;
};

struct FormatResult {
    bool accepted = false;
    std::string status;
};

struct SetDatetimeResult {
    bool accepted = false;
    std::string datetime;
};

struct WorkModeResult {
    bool accepted = false;
    int mode = 0;
};

} // namespace service

#endif // SERVICE_TYPES_H
