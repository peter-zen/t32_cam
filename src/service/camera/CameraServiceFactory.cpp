#include "CameraServiceFactory.h"
#include "impl/CameraServiceSim.h"
#ifndef SIMULATION_MODE
#include "impl/CameraServiceT32.h"
#endif

namespace service {

std::shared_ptr<ICameraService> CameraServiceFactory::create() {
#ifdef SIMULATION_MODE
    return std::make_shared<CameraServiceSim>();
#else
    return std::make_shared<CameraServiceT32>();
#endif
}

} // namespace service
