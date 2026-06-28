#pragma once
// SlotOutputPort — slot 1 → slot 2 的唤醒信号通道（signal，非 data）。
//
// 设计见 doc/knowledge/specs/wm-task-slot-scheduler.md §1.1/§1.2。wrapper 在
// capture-Done（events.captureCompleted）时 push() 一个 wake token；UploadTask
// 扫空 SD 后 parkIfNoWork() 等 token 唤醒重扫。真实 desc 路径不在 port 里——upload
// 自扫 SD（持久源）。
//
// 关键：封装 token 计数 + 消费者 idle 状态于同一 mutex 下，使 Done 判定
// （isSettled = idle 且无未消费 token）与线程消费 token 原子化——杜绝「线程消费
// token 但未重扫、poll 误判 Done」导致漏传 desc 的 TOCTOU（见 spec §1.2 修订注）。

#include <condition_variable>
#include <mutex>

namespace app_workmode {

class SlotOutputPort {
public:
    // wrapper 在 capture-Done 时调：push 一个 wake token。
    void push() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++tokens_;
        }
        cv_.notify_one();
    }

    // UploadTask::stop() 调：唤醒 parkIfNoWork() 中阻塞的线程令其退出。
    void signalStop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        cv_.notify_all();
    }

    // 线程：每次扫描 pass 前调，标记 busy（非 idle）。
    void setBusy() {
        std::lock_guard<std::mutex> lock(mutex_);
        idle_ = false;
    }

    // 线程：扫描无活后调。若 pass 期间已有 token 到达则立刻返回 true（重扫）；
    // 否则 parked(idle=true) 等 token/stop。返回 true=应重扫，false=已 stop。
    bool parkIfNoWork() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopped_) return false;
        if (tokens_ > 0) { tokens_ = 0; return true; }  // 扫描期间到达的活
        idle_ = true;
        cv_.wait(lock, [this] { return tokens_ > 0 || stopped_; });
        idle_ = false;
        if (stopped_) return false;
        tokens_ = 0;
        return true;
    }

    // poll()/diag：消费者 parked 且无未消费 token ⇒ 可安全 Done（scheduler 在
    // slot1 空时调 task->poll()，见此即标 Done）。
    bool isSettled() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return idle_ && tokens_ == 0;
    }

    // diag：消费者忙（扫描/上传中）。
    bool isBusy() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !idle_;
    }

    // diag：有未消费 token（capture 产活未扫）。
    bool hasPending() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tokens_ > 0;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    int tokens_ = 0;
    bool idle_ = false;    // 消费者 parked（扫描空 + 无 token 时置位）
    bool stopped_ = false;
};

}  // namespace app_workmode
