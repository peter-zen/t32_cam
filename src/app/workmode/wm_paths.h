#pragma once

#include <memory>
#include <string>

#include "StoragePaths.h"   // 完整定义——inline wm*() 调 ->mediaRoot()/dataDb()

namespace app_workmode {

// S3: wm 路径委托进程级 StoragePaths（wm_app 启动时 setStorage 注入）。
// wm task（RecordTask/SnapTask/WmScheduler）仍调 wmMediaPath()/wmUploadPath()，
// 内部统一读 StoragePaths —— 消除 /mnt/huntcam 硬编码，让 StoragePaths 成唯一来源。
// setStorage 必须在任一 wm*() 调用前执行（wm_app main 早期保证）；未 set 时返回空串。

inline std::shared_ptr<storage::StoragePaths>& wmStorageRef() {
    static std::shared_ptr<storage::StoragePaths> sp;
    return sp;
}

inline void setStorage(std::shared_ptr<storage::StoragePaths> sp) {
    wmStorageRef() = std::move(sp);
}

inline const std::shared_ptr<storage::StoragePaths>& wmStorage() {
    return wmStorageRef();
}

inline std::string wmMediaRoot() {
    return wmStorage() ? wmStorage()->mediaRoot() : std::string();
}

inline std::string wmMediaPath() {
    return wmMediaRoot() + "/";
}

inline std::string wmUploadPath() {
    return wmMediaRoot() + "/upload/";
}

inline std::string wmDbPath() {
    return wmStorage() ? wmStorage()->dataDb() : std::string();
}

}  // namespace app_workmode
