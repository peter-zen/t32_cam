#include "DeviceServiceSim.h"

namespace service {

DeviceInfo DeviceServiceSim::getDeviceInfo() {
    DeviceInfo info;
    info.pid = "T32-CAM-001";
    info.firmwareVersion = "1.0.0";
    info.model = "T32";
    info.buildDate = "2025-01-06";
    info.mcuVersion = "MCU-1.0.0";
    return info;
}

} // namespace service
