#include "SensorServiceSim.h"
#include <ctime>

namespace service {

SensorData SensorServiceSim::getSensorData() {
    SensorData data;
    data.batteryVoltage = 3700;
    data.batteryType = 1;
    data.batteryLevel = 85;
    data.externalVoltage = 12000;
    data.sdcardCapacity = 32000;
    data.sdcardUsed = 8000;
    data.cds = 500;
    data.temperature = 25;
    data.pressure = 1013;
    data.humidity = 60;

    char buf[32];
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.000", tm_info);
    data.datetime = buf;

    return data;
}

} // namespace service
