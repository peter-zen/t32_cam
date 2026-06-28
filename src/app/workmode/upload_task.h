#pragma once
// UploadTask — type=2 wm task。新建 wm 私有上传实现，移植 UploadWorker 的 lazy
// connect/auth + per-desc 上传逻辑（upload_worker.cpp），但用「扫 SD 取 desc」替代
// UploadWorker 的内存队列，并用 SlotOutputPort(signal) 唤醒。
//
// 见 doc/knowledge/specs/wm-task-slot-scheduler.md §1/§4 与 wm-app-spec.md §8。
// 不复用、不修改 legacy 共享的 UploadWorker（后者仍服务于 htc_workmode_app）。
//
// 生命周期：start() 起线程；线程循环 = 扫 SD 上传所有 F_UploadedTag==0 → 若无活则
// parkIfNoWork() 等 wake token。poll() 仅在 scheduler 允许完成（slot1 空）时被调，
// 看到 SlotOutputPort::isSettled() 即标 Done。线程从不自行 Done——终止由 scheduler
// 经 poll→Done→析构→stop() 驱动，保证 idle-grace→shutdown 可达。

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "wm_task_scheduler.h"  // WmTask

namespace network { class MgmtServClient; class StorageServClient; }

namespace app_workmode {

class SlotOutputPort;

class UploadTask : public WmTask {
public:
    // wakePort 由调用方（WmScheduler wrapper）拥有并注入；uploadDir = wmUploadPath()。
    // taskId 由调用方从共享 id 池传入，保证与 CaptureTask 的 id 全局唯一（日志可读）。
    UploadTask(SlotOutputPort& wakePort, std::string mgmtAddr, int mgmtPort,
               std::string uploadDir, int taskId);
    ~UploadTask() override;

    UploadTask(const UploadTask&) = delete;
    UploadTask& operator=(const UploadTask&) = delete;

    TaskType type() const override { return TaskType::Upload; }
    TaskState state() const override { return state_.load(); }
    int traceId() const override { return taskId_; }

    bool start() override;
    void poll(int64_t nowMs) override;
    void stop() override;

    int64_t timeoutAgeMs(int64_t nowMs, int64_t defaultAgeMs) const override;

private:
    void runLoop();
    bool ensureConnected();              // 移植自 UploadWorker::ensureConnected
    bool scanAndUploadOnePass();         // 扫 uploadDir 下 desc，上传所有 pending；返回是否做了真实工作
    bool hasPendingWork(const std::string& descPath) const;  // 解析 desc，判断是否有 F_UploadedTag==0
    void uploadOneDesc(const std::string& descPath);         // 移植自 UploadWorker::uploadOneDesc
    void abortBlockingIO();              // 断 mgmt/storage socket（SIGTERM 风格，移植自 UploadWorker::stop）

    SlotOutputPort& wakePort_;
    std::string mgmtAddr_;
    int mgmtPort_;
    std::string uploadDir_;

    std::thread worker_;
    std::atomic<TaskState> state_{TaskState::Ready};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> connected_{false};
    int taskId_ = 0;
    int64_t startedAtMs_ = 0;
    // activity-based upload-timeout：每次有进展（传完 desc / 被 wake token 唤醒 / connect
    // 成功）刷新；timeoutAgeMs 返回 now-lastActivityMs_。让多 trigger 长寿命 upload 不被
    // 「活太久」误杀——只要持续有进展就不超时，真卡死（无进展）才超时。
    // worker 线程写、scheduler 主线程读 → atomic。
    std::atomic<int64_t> lastActivityMs_{0};

    // mgmt_/storage_ 在 worker 线程写、abortBlockingIO(stop) 读——用 connMutex_ 守。
    std::mutex connMutex_;
    std::shared_ptr<network::MgmtServClient> mgmt_;
    std::shared_ptr<network::StorageServClient> storage_;
};

}  // namespace app_workmode
