#ifndef DEVICE_SERVICE_SIM_H
#define DEVICE_SERVICE_SIM_H

#include "../IDeviceService.h"

namespace service {

class DeviceServiceSim : public IDeviceService {
public:
    DeviceInfo getDeviceInfo() override;
};

} // namespace service

#endif // DEVICE_SERVICE_SIM_H
