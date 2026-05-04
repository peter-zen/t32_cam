#include "ServiceProvider.h"

#ifdef SIMULATION_MODE
#include "device/impl/DeviceServiceSim.h"
#include "sensor/impl/SensorServiceSim.h"
#include "storage/impl/StorageServiceSim.h"
#include "system/impl/SystemServiceSim.h"
#else
#include "device/impl/DeviceServiceT32.h"
#include "sensor/impl/SensorServiceT32.h"
#include "storage/impl/StorageServiceT32.h"
#include "system/impl/SystemServiceT32.h"
#endif

namespace service {

std::shared_ptr<IDeviceService> ServiceProvider::createDeviceService() {
#ifdef SIMULATION_MODE
    return std::make_shared<DeviceServiceSim>();
#else
    return std::make_shared<DeviceServiceT32>();
#endif
}

std::shared_ptr<ISensorService> ServiceProvider::createSensorService() {
#ifdef SIMULATION_MODE
    return std::make_shared<SensorServiceSim>();
#else
    return std::make_shared<SensorServiceT32>();
#endif
}

std::shared_ptr<IStorageService> ServiceProvider::createStorageService() {
#ifdef SIMULATION_MODE
    return std::make_shared<StorageServiceSim>();
#else
    return std::make_shared<StorageServiceT32>();
#endif
}

std::shared_ptr<ISystemService> ServiceProvider::createSystemService() {
#ifdef SIMULATION_MODE
    return std::make_shared<SystemServiceSim>();
#else
    return std::make_shared<SystemServiceT32>();
#endif
}

} // namespace service
