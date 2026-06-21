#pragma once

#include <string>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <memory>
#include <chrono>

namespace network { class MgmtServClient; class StorageServClient; }

namespace app_workmode {

// 后台上传 worker：消费 desc 文件路径队列，每个 desc 走完整上传链路
// （解析 → 校验 PID → uploadFile(desc) → uploadFile(file_inf) → 回写 F_UploadedTag
//  → 按 FILE_MANAGE 删除策略）。录影/拍照线程只需 enqueue(desc) 即可返回，不被上传阻塞。
//
// 并发模型：单 worker 线程串行消费 desc；文件级上传复用 StorageServClient 自带的
// uploadThread（send(socket_fd) 只在该线程跑）。worker 线程仅在 start() 时 connect+auth
// （此时上传队列空，不与 uploadThread 的 send 并发）。故 -wm 0（无心跳）场景下 socket
// send 不并发，暂不需要 send_mutex；若后续事件循环加入心跳/下发并发 send，需给
// Client::sendMessage + StorageServClient::upload 的 ::send 加共享锁。
class UploadWorker {
public:
    UploadWorker();
    ~UploadWorker();

    // 启动 worker：连接 mgmt server + authenticate + 建 StorageServClient（会话级，一次）。
    // mgmt 连接/鉴权失败时 worker 仍可 enqueue，但上传会失败（desc 留 SD，F_UploadedTag=0）。
    void start(const std::string& mgmtAddr, int mgmtPort);

    // 录影/拍照产物 desc 落盘后入队。线程安全、非阻塞。
    void enqueue(const std::string& descPath);

    // 关机协调：等队列排空或超时（timeoutMs）。返回是否全部处理完。
    bool flush(int timeoutMs);

    // 停止 worker（flush 后调用）。
    void stop();

    // 队列状态查询（供 EventLoop 判断上传完成/超时关机）。
    // isIdle() = 队列空 且 当前无在途上传（worker 已出队但未传完）。
    //   必须用 isIdle() 而非"队列是否空"判定上传完成：worker 一旦 pop，队列即空，
    //   但 connect/auth/uploadFile 仍在锁外进行（耗时数秒）。若按"队列空"判完成，
    //   会在上传中途误判完成 → 提前关机（见 logs/debug.log 2026-06-21）。
    bool isIdle();
    int64_t firstEnqueueAgeMs();  // 有未完成工作(在队或在途)时返回首次入队至今的毫秒；完全空闲返回 0

private:
    void workerLoop();
    // 平移自 WorkModeRunner.cpp CMD_UPLOAD 的 per-desc 循环体（原 :742-855）。
    void uploadOneDesc(const std::string& descPath);

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::thread worker_;
    bool started_ = false;
    bool stopRequested_ = false;
    std::shared_ptr<network::MgmtServClient> mgmt_;
    std::shared_ptr<network::StorageServClient> storage_;
    std::chrono::steady_clock::time_point firstEnqueueTime_;  // 本批工作(从空闲进入)的起始时刻
    bool busy_ = false;  // worker 已出队某 desc 但未传完(在途)；isIdle() 据此判定真正空闲

    // lazy connect：start() 只存配置，真正 connect/auth 延迟到 worker 线程第一次
    // 拿到 desc（即录影完成后）—— 避免启动时就做 DNS/socket/auth 拖慢录影启动。
    std::string mgmtAddr_;
    int mgmtPort_ = 0;
    bool connected_ = false;
    void ensureConnected();
};

}  // namespace app_workmode
