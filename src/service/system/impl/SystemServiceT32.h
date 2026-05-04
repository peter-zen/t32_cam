#ifndef SYSTEM_SERVICE_T32_H
#define SYSTEM_SERVICE_T32_H

#include "../ISystemService.h"

namespace service {

class SystemServiceT32 : public ISystemService {
public:
    SetDatetimeResult setDatetime(const std::string& datetime) override;
    WorkModeResult setWorkMode(int mode) override;
};

} // namespace service

#endif // SYSTEM_SERVICE_T32_H
