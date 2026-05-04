#ifndef SYSTEM_SERVICE_SIM_H
#define SYSTEM_SERVICE_SIM_H

#include "../ISystemService.h"

namespace service {

class SystemServiceSim : public ISystemService {
public:
    SetDatetimeResult setDatetime(const std::string& datetime) override;
    WorkModeResult setWorkMode(int mode) override;
};

} // namespace service

#endif // SYSTEM_SERVICE_SIM_H
