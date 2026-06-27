#pragma once
#include <atomic>
#include <memory>
#include "snap_task.h"
#include "record_task.h"

namespace app_workmode {

// CaptureLane — 按 Settings::cameraMode（wm-app-spec §5.1：0=仅拍照 / 1=拍照+录影 /
// 2=仅录影）路由 trigger。cm==1 顺序：先拍照（同步完成）再录影（异步）——避免并发
// 拍录的 CH2 硬件缩放器争用（cm==3 并发不在 wm 范围）。isBusy = snap 在途（lane
// 维度记录，跟 SnapTask 内部状态无关）OR record 在途 ——供 wm_scheduler 的 PIR
// 门在 cm==1 原子语义下严格屏蔽并发触发。
class CaptureLane {
public:
    explicit CaptureLane(std::shared_ptr<UploadWorker> uploadWorker);

    // 读 cameraMode 路由：0→SnapTask；2→RecordTask；1→SnapTask(同步等完)→RecordTask。
    // 返回是否成功接受（snap 同步完 + record 入队）。
    bool trigger();

    // cm==1 原子判定：snap 在途（lane 记录）或 record 在录（record 自报）均视为 busy。
    // wm_scheduler 据此屏蔽并发 PIR。
    bool isBusy() const;

    // 停 record + 等 onComplete（snap 同步无需停）。
    void stop();

private:
    std::shared_ptr<SnapTask>   snap_;
    std::shared_ptr<RecordTask> record_;
    // cm==1 原子语义辅助：trigger 进入即置位，RAII guard 在所有 return 路径清位。
    // 200ms 同步 snap 期间给 PIR gate 一个"绝对不在"的硬信号。
    std::atomic<bool>           snap_busy_;
};

}  // namespace app_workmode
