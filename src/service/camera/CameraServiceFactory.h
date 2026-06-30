#ifndef CAMERA_SERVICE_FACTORY_H
#define CAMERA_SERVICE_FACTORY_H

#include <memory>
#include "ICameraService.h"

namespace service {

class CameraServiceFactory {
public:
    static std::shared_ptr<ICameraService> create();
    // 进程级单例：让 um bring-up 的 prewarm 与 HTTP handler 的 get_camera_service 共享同一实例。
    static std::shared_ptr<ICameraService> getInstance();
};

} // namespace service

#endif // CAMERA_SERVICE_FACTORY_H
