#ifndef SERVICE_PROVIDER_H
#define SERVICE_PROVIDER_H

#include <memory>
#include "device/IDeviceService.h"
#include "sensor/ISensorService.h"
#include "storage/IStorageService.h"
#include "system/ISystemService.h"

namespace service {

class ServiceProvider {
public:
    static std::shared_ptr<IDeviceService> createDeviceService();
    static std::shared_ptr<ISensorService> createSensorService();
    static std::shared_ptr<IStorageService> createStorageService();
    static std::shared_ptr<ISystemService> createSystemService();
};

} // namespace service

#endif // SERVICE_PROVIDER_H
