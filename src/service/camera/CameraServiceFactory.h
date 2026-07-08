#ifndef CAMERA_SERVICE_FACTORY_H
#define CAMERA_SERVICE_FACTORY_H

#include <memory>
#include "ICameraService.h"

namespace storage { class StoragePaths; }

namespace service {

class CameraServiceFactory {
public:
    // storage 必须非空——CameraService 的所有媒体/db 路径都从它取（S3 路径注入）。
    static std::shared_ptr<ICameraService> create(std::shared_ptr<storage::StoragePaths> storage);
    // 进程级单例：让 um bring-up 的 prewarm 与 HTTP handler 的 get_camera_service 共享同一实例。
    // 首次调用必须带 storage（um_app prewarm 在 HTTP server 启动前 → 顺序保证）；之后无参拿缓存。
    static std::shared_ptr<ICameraService> getInstance(std::shared_ptr<storage::StoragePaths> storage = nullptr);
};

} // namespace service

#endif // CAMERA_SERVICE_FACTORY_H
