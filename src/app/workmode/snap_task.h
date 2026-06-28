#pragma once
#include <memory>

namespace app_workmode {

class UploadWorker;

// 拍照捕获 lane（RecordTask 的兄弟，wm-app-spec §5）。trigger() **同步**拍照
// （~1-2s，在调用线程，同 snap_test——避免 worker 线程碰 IMP 的线程安全问题），
// 内部：ImageSnap::snap（写 JPG + 内部 addMedia type=1）→ 显式 saveThumbnail →
// createDescInfoFile 落盘。uploadWorker 非空时 enqueue（legacy workmode_app 用）；
// wm 路径传 nullptr，desc 只落盘、由 type 2 UploadTask 自扫上传。产物 = 文件 +
// 缩略图DB + 元数据DB + desc(F_UploadedTag=0)。
//
// 每次 trigger 新建 ImageSnap（destruct 释放 channel，避免跨拍碰撞）；进程内不
// IMP_System_Exit（sharedVideo 单例）。同步 ⇒ isBusy() 恒 false（trigger 返回即完成）。
class SnapTask {
public:
    explicit SnapTask(std::shared_ptr<UploadWorker> uploadWorker);
    ~SnapTask();

    // 同步拍照：阻塞到拍完 + 后处理完。返回是否成功（snap + desc 落盘/入队）。
    bool trigger();

    // 同步任务：trigger 返回后即空闲。
    bool isBusy() const { return false; }

    // 无在途任务（同步），no-op。
    void stop() {}

private:
    std::shared_ptr<UploadWorker> uploadWorker_;  // nullptr（wm）则只落盘不 enqueue
};

}  // namespace app_workmode
