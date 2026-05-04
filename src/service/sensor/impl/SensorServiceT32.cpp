#include "SensorServiceT32.h"

#ifndef SIMULATION_MODE

namespace service {

// TODO: Replace with real MCU/Disk calls
// MCU::getInstance()->readBatteryVoltage(), readTemperature(), readHumidity(), etc.
// Disk::getInfo() for SD card capacity
SensorData SensorServiceT32::getSensorData() {
    SensorData data;
    data.batteryVoltage = 0;
    data.batteryType = 0;
    data.batteryLevel = 0;
    data.externalVoltage = 0;
    data.sdcardCapacity = 0;
    data.sdcardUsed = 0;
    data.cds = 0;
    data.temperature = 0;
    data.pressure = 0;
    data.humidity = 0;
    return data;
}

} // namespace service

#endif // SIMULATION_MODE
