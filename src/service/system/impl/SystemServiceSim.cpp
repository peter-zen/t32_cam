#include "SystemServiceSim.h"
#include <elog.h>

namespace service {

SetDatetimeResult SystemServiceSim::setDatetime(const std::string& datetime) {
    elog_i("sys_svc", "Simu: requested datetime update: %s", datetime.c_str());
    SetDatetimeResult result;
    result.accepted = true;
    result.datetime = datetime;
    return result;
}

WorkModeResult SystemServiceSim::setWorkMode(int mode) {
    elog_i("sys_svc", "Simu: requested work mode switch: %d", mode);
    WorkModeResult result;
    result.accepted = true;
    result.mode = mode;
    return result;
}

} // namespace service
