#include "SensorServiceT32.h"

#ifndef SIMULATION_MODE

#include "../../mcu/McuService.h"

namespace service {

SensorData SensorServiceT32::getSensorData() {
    auto& mcu = service::McuService::getInstance();
    SensorData data;
    data.batteryVoltage = mcu.getBattery1Voltage();
    data.batteryType = mcu.getBatteryType();
    data.batteryLevel = mcu.getBatteryLevel();
    data.externalVoltage = mcu.getExternalVoltage();
    // sdcardCapacity / sdcardUsed are owned by storage, not MCU.
    data.sdcardCapacity = 0;
    data.sdcardUsed = 0;
    data.cds = mcu.getCds();
    data.temperature = mcu.getTemperature();
    data.pressure = mcu.getAtmosPressure();
    data.humidity = mcu.getHumidity();
    return data;
}

} // namespace service

#endif // SIMULATION_MODE
