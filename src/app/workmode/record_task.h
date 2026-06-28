#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace service { namespace camera { class CameraRecorder; struct RecordResult; } }

namespace app_workmode {

class UploadWorker;

// 长驻录影任务（阶段2）：复用单个 CameraRecorder 实例，trigger() 异步录影，
// onComplete 里 saveThumbnail + releaseVideoResources + malloc_trim + desc 落盘。
// uploadWorker 非空时 enqueue（legacy workmode_app 用）；wm 路径传 nullptr，
// desc 只落盘、由 type 2 UploadTask 自扫上传。is_recording_ 拒并发（PIR 连发时丢弃
// 重叠触发）—— Ingenic SDK 不支持并发主码流。
//
// 关键约束：同进程内反复 record/stop，不重新 IMP_System_Init（规避跨进程
// tisp_awb_init 崩溃）。事件循环（阶段3）调用，长驻直到关机信号。
class RecordTask {
public:
    explicit RecordTask(std::shared_ptr<UploadWorker> uploadWorker);
    ~RecordTask();

    // 触发一次录影（异步：record 后台跑，完时 onComplete 回调）。
    // 返回 false = 上一次还没结束（拒并发）。
    bool trigger();

    // 当前是否在录影（EventLoop 判断上传完成关机用）。
    bool isRecording() const { return recording_.load(); }

    // 已成功完成（None/UserStop）的录影段数（EventLoop 的 HTC_TEST_RECORD_COUNT 门控用）。
    int completedCount() const { return completedCount_.load(); }

    // 关机协调：停当前录影（若有）+ 等 onComplete 收尾。
    void stop();

private:
    void onCompleteRecord(const std::string& recordPath, const service::camera::RecordResult& r);

    std::shared_ptr<UploadWorker> uploadWorker_;  // nullptr（wm）则只落盘不 enqueue
    std::shared_ptr<service::camera::CameraRecorder> recorder_;
    std::atomic<bool> recording_{false};
    std::atomic<int> completedCount_{0};  // 成功完成的录影段数（测试门控）
};

}  // namespace app_workmode
