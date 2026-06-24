#pragma once
#include <memory>
#include "snap_task.h"
#include "record_task.h"

namespace app_workmode {

// CaptureLane — 按 Settings::cameraMode（wm-app-spec §5.1：0=仅拍照 / 1=拍照+录影 /
// 2=仅录影）路由 trigger。cm==1 顺序：先拍照（同步完成）再录影（异步）——避免并发
// 拍录的 CH2 硬件缩放器争用（cm==3 并发不在 wm 范围）。isBusy = snap 或 record 在途。
class CaptureLane {
public:
    explicit CaptureLane(std::shared_ptr<UploadWorker> uploadWorker);

    // 读 cameraMode 路由：0→SnapTask；2→RecordTask；1→SnapTask(同步等完)→RecordTask。
    // 返回是否成功接受（snap 同步完 + record 入队）。
    bool trigger();

    bool isBusy() const;   // snap 在途（恒 false，同步）或 record 在录
    void stop();           // 停 record + 等 onComplete（snap 同步无需停）

private:
    std::shared_ptr<SnapTask>   snap_;
    std::shared_ptr<RecordTask> record_;
};

}  // namespace app_workmode
