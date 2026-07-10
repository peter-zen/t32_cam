#pragma once
// WmScheduler — wm 的 3 模式任务调度器（wm-app-spec §3）的 runtime wrapper。
//
// 结构：WmTaskSchedulerCore（纯调度内核，fs/HAL-agnostic）+ Capture lane(type 1) +
// Upload lane(type 2, 新 wm 私有 UploadTask) + SlotOutputPort(slot1→slot2 wake signal)
// + 终态 Shutdown。Capture/Upload 并发不互斥。关机自管：全 slot 空持续 idleGraceMs
// → Shutdown；Upload 超时→requestShutdown(SIGTERM)；外部信号→Shutdown。Shutdown
// 屏蔽触发、不可中断。
//
// m0(CaptureOnly)=仅 Capture；m1(CaptureUpload)=Capture+Upload；m2(UploadOnly)=仅 Upload；
// m3(Heartbeat)=单次心跳+poweroff（lean，不走 slot 模型）。
//
// 注：不再持有 legacy 共享的 UploadWorker——type 2 由新 UploadTask 自扫 SD 上传。
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "slot_output_port.h"
#include "wm_task_scheduler.h"

namespace app_lifecycle { class ProcessLifecycle; }

namespace app_workmode {

class CaptureLane;
class IPirTrigger;

class WmScheduler {
public:
    // capture/trigger 可为 null（m2 无捕获）。mgmtAddr/mgmtPort 仅 m1/m2 用（m0 传空）。
    // workDirs：m2 待扫的工作目录列表（quickSnap 目录 + 兜底滞留目录）；m1/m0/m3 传空
    //   （m1 仍走 wmUploadPath() 单目录扫描，不受影响）。
    WmScheduler(app_lifecycle::ProcessLifecycle& lc, WmMode mode,
                std::shared_ptr<CaptureLane> capture,
                std::shared_ptr<IPirTrigger> trigger,
                std::string mgmtAddr, int mgmtPort,
                int64_t idleGraceMs, int64_t uploadTimeoutMs,
                std::vector<std::string> workDirs = {});
    ~WmScheduler();

    // 长驻直到关机（idle-grace/upload-timeout/信号）。run() 末尾 stop capture lane
    // （wm_app 尾随后做 lc.shutdown=IMP teardown）。
    void run();

private:
    void runHeartbeat();  // m3: connect+auth+sendHeartbeat 一次 → return（不走 slot 模型）

    app_lifecycle::ProcessLifecycle& lc_;
    WmMode  mode_;
    std::shared_ptr<CaptureLane>  capture_;
    std::shared_ptr<IPirTrigger> trigger_;
    std::string mgmtAddr_;
    int mgmtPort_ = 0;
    int64_t idleGraceMs_;
    int64_t uploadTimeoutMs_;
    std::vector<std::string> workDirs_;  // m2 工作目录列表（m1/m0/m3 空）
    SlotOutputPort wakePort_;  // slot1→slot2 唤醒信号（capture-Done 时 push）
};

}  // namespace app_workmode
