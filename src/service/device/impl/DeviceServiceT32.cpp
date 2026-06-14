#include "DeviceServiceT32.h"

#ifndef SIMULATION_MODE

#include <elog.h>

#include "../../mcu/McuService.h"

namespace service {

DeviceInfo DeviceServiceT32::getDeviceInfo() {
    auto& mcu = service::McuService::getInstance();
    DeviceInfo info;
    info.pid = mcu.getPID();
    info.firmwareVersion = mcu.getFirmwareVersion();
    info.model = "T32";
    info.buildDate = "";
    info.mcuVersion = mcu.getMcuFirmwareVersion();
    return info;
}

} // namespace service

#endif // SIMULATION_MODE
