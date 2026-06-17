#include "SystemServiceT32.h"

#ifndef SIMULATION_MODE

#include <elog.h>

#include "../../mcu/McuService.h"

namespace service {

SetDatetimeResult SystemServiceT32::setDatetime(const std::string& datetime) {
    SetDatetimeResult result;
    if (!service::McuService::getInstance().setDatetime(datetime)) {
        elog_w("sys_svc", "T32: datetime parse/reject for '%s'", datetime.c_str());
        result.accepted = false;
        result.datetime = datetime;
        return result;
    }
    elog_i("sys_svc", "T32: datetime applied: %s", datetime.c_str());
    result.accepted = true;
    result.datetime = datetime;
    return result;
}

// Work mode is firmware-set via GPIO / MCU firmware; there is no I2C write
// path. The HTTP layer is expected to map accepted=false into a 501
// response — see api_v1_system_workmode in http_api_v1.cpp.
WorkModeResult SystemServiceT32::setWorkMode(int mode) {
    elog_w("sys_svc", "T32: setWorkMode(%d) rejected (work mode is firmware-only)", mode);
    WorkModeResult result;
    result.accepted = false;
    result.mode = mode;
    return result;
}

} // namespace service

#endif // SIMULATION_MODE
