#ifndef DEVICE_SERVICE_T32_H
#define DEVICE_SERVICE_T32_H

#include "../IDeviceService.h"

namespace service {

class DeviceServiceT32 : public IDeviceService {
public:
    DeviceInfo getDeviceInfo() override;
};

} // namespace service

#endif // DEVICE_SERVICE_T32_H
