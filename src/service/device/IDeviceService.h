#ifndef I_DEVICE_SERVICE_H
#define I_DEVICE_SERVICE_H

#include "../ServiceTypes.h"

namespace service {

class IDeviceService {
public:
    virtual ~IDeviceService() = default;
    virtual DeviceInfo getDeviceInfo() = 0;
};

} // namespace service

#endif // I_DEVICE_SERVICE_H
