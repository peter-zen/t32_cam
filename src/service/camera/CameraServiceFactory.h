#ifndef CAMERA_SERVICE_FACTORY_H
#define CAMERA_SERVICE_FACTORY_H

#include <memory>
#include "ICameraService.h"

namespace service {

class CameraServiceFactory {
public:
    static std::shared_ptr<ICameraService> create();
};

} // namespace service

#endif // CAMERA_SERVICE_FACTORY_H
