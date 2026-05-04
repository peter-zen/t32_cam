#ifndef I_SYSTEM_SERVICE_H
#define I_SYSTEM_SERVICE_H

#include "../ServiceTypes.h"

namespace service {

class ISystemService {
public:
    virtual ~ISystemService() = default;
    virtual SetDatetimeResult setDatetime(const std::string& datetime) = 0;
    virtual WorkModeResult setWorkMode(int mode) = 0;
};

} // namespace service

#endif // I_SYSTEM_SERVICE_H
