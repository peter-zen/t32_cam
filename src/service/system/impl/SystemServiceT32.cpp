#include "SystemServiceT32.h"

#ifndef SIMULATION_MODE

#include <elog.h>

namespace service {

// TODO: Parse ISO string, call MCU::getInstance()->setDatetime() + settimeofday()
SetDatetimeResult SystemServiceT32::setDatetime(const std::string& datetime) {
    elog_i("sys_svc", "T32: requested datetime update: %s (not yet implemented)", datetime.c_str());
    SetDatetimeResult result;
    result.accepted = true;
    result.datetime = datetime;
    return result;
}

// TODO: Call MCU::getInstance() for work mode control
WorkModeResult SystemServiceT32::setWorkMode(int mode) {
    elog_i("sys_svc", "T32: requested work mode switch: %d (not yet implemented)", mode);
    WorkModeResult result;
    result.accepted = true;
    result.mode = mode;
    return result;
}

} // namespace service

#endif // SIMULATION_MODE
