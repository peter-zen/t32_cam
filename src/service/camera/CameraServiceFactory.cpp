#include "CameraServiceFactory.h"
#include "impl/CameraServiceSim.h"
#ifndef SIMULATION_MODE
#include "impl/CameraServiceT32.h"
#endif
#include "StoragePaths.h"

#include <cstdio>
#include <cstdlib>

namespace service {

std::shared_ptr<ICameraService> CameraServiceFactory::create(std::shared_ptr<storage::StoragePaths> storage) {
    if (!storage) {
        std::fprintf(stderr, "CameraServiceFactory::create requires non-null StoragePaths — aborting\n");
        std::abort();
    }
#ifdef SIMULATION_MODE
    return std::make_shared<CameraServiceSim>(storage);
#else
    return std::make_shared<CameraServiceT32>(storage);
#endif
}

std::shared_ptr<ICameraService> CameraServiceFactory::getInstance(std::shared_ptr<storage::StoragePaths> storage) {
    static std::shared_ptr<ICameraService> instance = create(storage);
    return instance;
}

} // namespace service
