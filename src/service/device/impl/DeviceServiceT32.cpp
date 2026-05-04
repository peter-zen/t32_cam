#include "DeviceServiceT32.h"

#ifndef SIMULATION_MODE

namespace service {

// TODO: Replace with real MCU calls
// MCU::getInstance()->readPID()
// MCU::getInstance()->readFirmwareVersion()
DeviceInfo DeviceServiceT32::getDeviceInfo() {
    DeviceInfo info;
    info.pid = "T32-CAM-001";
    info.firmwareVersion = "0.0.0";
    info.model = "T32";
    info.buildDate = "";
    info.mcuVersion = "";
    return info;
}

} // namespace service

#endif // SIMULATION_MODE
