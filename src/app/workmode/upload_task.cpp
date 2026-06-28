// UploadTask — see upload_task.h. New wm-private type=2 task: SD-scan driven upload
// (no in-memory queue), woken by SlotOutputPort signal. connect/auth + per-desc upload
// logic ported verbatim from UploadWorker (upload_worker.cpp) — proven path.

#include "upload_task.h"

#include "slot_output_port.h"

#include "MgmtServClient.h"
#include "StorageServClient.h"
#include "DeviceConfig.h"
#include "Common.h"            // EC_SUCCESS, INI_*
#include "misc/Misc.h"         // listFilenames / isJsonFile / deleteFile
#include "Logger.h"

#include <json/json.h>
#include <fstream>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <unistd.h>

using namespace network;

namespace app_workmode {

namespace {

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t connectGraceMs() {
    const char* env = std::getenv("HTC_UPLOAD_CONNECT_GRACE_MS");
    if (env && env[0] != '\0') {
        int v = std::atoi(env);
        if (v > 0) return v;
    }
    return 30000;
}

}  // namespace

UploadTask::UploadTask(SlotOutputPort& wakePort, std::string mgmtAddr, int mgmtPort,
                       std::string uploadDir, int taskId)
    : wakePort_(wakePort), mgmtAddr_(std::move(mgmtAddr)), mgmtPort_(mgmtPort),
      uploadDir_(std::move(uploadDir)), taskId_(taskId) {}

UploadTask::~UploadTask() {
    stop();
}

bool UploadTask::start() {
    TaskState expected = TaskState::Ready;
    if (!state_.compare_exchange_strong(expected, TaskState::Running)) return false;
    startedAtMs_ = steadyNowMs();
    worker_ = std::thread(&UploadTask::runLoop, this);
    return true;
}

void UploadTask::poll(int64_t /*nowMs*/) {
    // 仅在 scheduler 允许完成（slot1 空）时被 TaskSlot::poll 调。
    // isSettled() = 消费者 parked 且无未消费 token（同 mutex 原子，杜绝漏传 race）。
    if (state_.load() == TaskState::Running && wakePort_.isSettled()) {
        state_.store(TaskState::Done);
    }
}

void UploadTask::stop() {
    TaskState s = state_.load();
    if (s == TaskState::Running) {
        stopRequested_.store(true);
        state_.store(TaskState::Stopping);
    }
    wakePort_.signalStop();        // 唤醒 parkIfNoWork() 中的线程
    abortBlockingIO();             // 断阻塞 recv/send（SIGTERM 风格）
    if (worker_.joinable()) worker_.join();
    if (state_.load() == TaskState::Stopping) {
        state_.store(TaskState::Done);
    }
}

int64_t UploadTask::timeoutAgeMs(int64_t /*nowMs*/, int64_t /*defaultAgeMs*/) const {
    TaskState s = state_.load();
    if (s != TaskState::Running && s != TaskState::Stopping) return 0;
    int64_t age = steadyNowMs() - startedAtMs_;
    // 未 connected 时给 connect grace，避免慢 auth 被超时误杀；超 grace 后如实返回 age。
    if (!connected_.load() && age < connectGraceMs()) return 0;
    return age;
}

void UploadTask::runLoop() {
    // HTC_UPLOAD_DIAG=1：默认静默，仅异常时打点（grill 2026-06-28：scan-gap RCA 已完成，
    // 逐行打点失去意义）。两类异常：① 慢空扫盘（didWork=0 却 dt 大 → NFS dir 反常慢）；
    // ② token-spin（连续 park 醒来却无活）。正常路径只产 runLoop exit 一行。
    const bool diag = (std::getenv("HTC_UPLOAD_DIAG") != nullptr);
    int idleWakes = 0;   // 连续「park 醒来却 didWork=0」计数（spin 检测；正常 drain ≤1）
    while (!stopRequested_.load()) {
        wakePort_.setBusy();
        // 未连上 mgmt 时不要扫盘：否则 uploadOneDesc 因无 storage 全部 skip 却把
        // didWork 置 true → 立即重扫 → connect-fail 死循环（busy spin）。connect 失败
        // 就 park，靠超时（HTC_UPLOAD_TIMEOUT_MS）或下一次 capture-Done 的 wake 重试。
        bool didWork = false;
        if (connected_.load() || ensureConnected()) {
            didWork = scanAndUploadOnePass();
        }
        if (stopRequested_.load()) break;
        if (didWork) {                  // 立即重扫（可能还有更多 desc）
            idleWakes = 0;
            continue;
        }
        bool woke = wakePort_.parkIfNoWork();   // 无活 → park 等 wake token / stop
        if (!woke) break;
        if (diag && ++idleWakes >= 3) {          // 连续 ≥3 次 park 醒来无活 → spin
            Logger::log(LogLevel::WARNING,
                        "[udiag] token-spin: %d consecutive idle wakes", idleWakes);
            idleWakes = 0;                       // 重置免刷屏
        }
    }
    if (diag) Logger::log(LogLevel::INFO, "[udiag] runLoop exit");
}

// 移植自 UploadWorker::ensureConnected（upload_worker.cpp:50-78）。
bool UploadTask::ensureConnected() {
    if (connected_.load()) return true;
    Logger::log(LogLevel::INFO, "UploadTask: connecting [%s:%d]", mgmtAddr_.c_str(), mgmtPort_);
    auto mgmt = std::make_shared<MgmtServClient>(mgmtAddr_, mgmtPort_);
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        mgmt_ = mgmt;
    }
    if (EC_SUCCESS != mgmt->connect(3000)) {
        Logger::log(LogLevel::ERROR, "UploadTask: connect [%s:%d] failed", mgmtAddr_.c_str(), mgmtPort_);
        return false;
    }
    Logger::log(LogLevel::INFO, "UploadTask: connected socket, authenticating");
    if (EC_SUCCESS != mgmt->authenticate()) {
        Logger::log(LogLevel::ERROR, "UploadTask: auth failed");
        return false;
    }
    Logger::log(LogLevel::INFO, "UploadTask: auth returned success, creating storage client");
    auto storage = mgmt->newStorageServClient();
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        storage_ = storage;
        connected_.store(true);
    }
    Logger::log(LogLevel::INFO, "UploadTask: connected + authed [%s:%d] (lazy, after slot 2 start)",
                mgmtAddr_.c_str(), mgmtPort_);
    return true;
}

void UploadTask::abortBlockingIO() {
    std::shared_ptr<StorageServClient> storage;
    std::shared_ptr<MgmtServClient> mgmt;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        storage = storage_;
        mgmt = mgmt_;
    }
    if (storage) storage->requestStop();
    if (mgmt) mgmt->shutdownSocket();
}

bool UploadTask::hasPendingWork(const std::string& descPath) const {
    std::ifstream ifs(descPath);
    if (!ifs) return false;
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(ifs, root)) return false;
    if (root["F_UploadedTag"].asInt() == 0) return true;
    const Json::Value& file_inf = root["file_inf"];
    if (file_inf.isArray()) {
        for (Json::ArrayIndex i = 0; i < file_inf.size(); ++i) {
            if (file_inf[i]["F_UploadedTag"].asInt() == 0) return true;
        }
    }
    return false;
}

bool UploadTask::scanAndUploadOnePass() {
    const bool diag = (std::getenv("HTC_UPLOAD_DIAG") != nullptr);
    const int64_t t0 = steadyNowMs();   // 始终计时（cheap）；仅异常时用
    std::vector<std::string> files = Misc::listFilenames(uploadDir_);
    bool didWork = false;
    for (const std::string& f : files) {
        if (stopRequested_.load()) break;
        std::string p = uploadDir_ + f;
        if (!Misc::isJsonFile(p)) continue;
        if (!hasPendingWork(p)) continue;   // 已传完的跳过
        uploadOneDesc(p);
        didWork = true;
    }
    // 异常才打：无活（didWork=0）却耗时 > 1s → 纯扫盘反常慢（原 scan-gap 症状）。
    // didWork=1 的大 dt 是 mp4 上传 I/O，属正常，不打。
    if (diag && !didWork) {
        int64_t dt = steadyNowMs() - t0;
        if (dt > 1000) {
            Logger::log(LogLevel::WARNING, "[udiag] slow idle scan: n=%d dt=%lldms",
                        (int)files.size(), (long long)dt);
        }
    }
    return didWork;
}

// 移植自 UploadWorker::uploadOneDesc（upload_worker.cpp:169-290）。
void UploadTask::uploadOneDesc(const std::string& desc_filename) {
    std::shared_ptr<StorageServClient> storage;
    {
        std::lock_guard<std::mutex> lock(connMutex_);
        storage = storage_;
    }
    if (!storage) {
        Logger::log(LogLevel::WARNING, "UploadTask: no storage client (not authed?), skip %s",
                    desc_filename.c_str());
        return;
    }
    Logger::log(LogLevel::INFO, "UploadTask: desc_filename %s", desc_filename.c_str());
    std::ifstream ifs(desc_filename);

    if (!Misc::isJsonFile(desc_filename)) {
        Logger::log(LogLevel::INFO, "%s is not json", desc_filename.c_str());
        Misc::deleteFile(desc_filename);
        return;
    }

    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(ifs, root)) {
        Logger::log(LogLevel::ERROR, "Failed to parse JSON file: %s", desc_filename.c_str());
        Misc::deleteFile(desc_filename);
        return;
    }

    std::string pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
    if (!root.isMember("F_UploadedTag") || !root.isMember("device") ||
        !root["device"].isMember("PID") || (root["device"]["PID"].asString() != pid)) {
        Logger::log(LogLevel::ERROR, "PID not match");
        Misc::deleteFile(desc_filename);
        return;
    }

    if (!root.isMember("file_inf")) {
        Logger::log(LogLevel::ERROR, "file_inf not exist");
        Misc::deleteFile(desc_filename);
        return;
    }

    bool descfile_uploaded = false;
    if (root["F_UploadedTag"].asInt() == 0) {
        auto target_filename = desc_filename;
        storage->bindUploadCallback([&descfile_uploaded, target_filename](const std::string& filename, int error_code) {
            Logger::log(LogLevel::INFO, "upload descfile [%s], error code: %d", filename.c_str(), error_code);
            if (filename == target_filename) {
                descfile_uploaded = (error_code == EC_SUCCESS) ? true : false;
            }
        });
        storage->uploadFile(desc_filename);
        auto start_time = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() < 8) {  // must > 5s
            if (descfile_uploaded) {
                Logger::log(LogLevel::INFO, "descfile %s uploaded", desc_filename.c_str());
                root["F_UploadedTag"] = 1;
                std::ofstream ofs(desc_filename);
                ofs << root.toStyledString();
                ofs.close();
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            now = std::chrono::steady_clock::now();
        }
    } else {
        descfile_uploaded = true;
    }

    // 同 UploadWorker：即便 desc 没收到 server ack，file_inf 里的媒体也总是尝试上传
    // （server 通常仍能收 upload 命令）。desc 自身 tag 落盘受上面 8s wait 保护。
    {
        bool allFileUploaded = true;
        const Json::Value file_inf_array = root["file_inf"];
        std::vector<std::string> uploaded_file_list;
        storage->bindUploadCallback([&uploaded_file_list, &allFileUploaded](const std::string& filename, int error_code) {
            Logger::log(LogLevel::INFO, "upload %s, error code: %d", filename.c_str(), error_code);
            if (error_code == EC_SUCCESS) {
                uploaded_file_list.push_back(filename);
            } else {
                allFileUploaded = false;
            }
        });
        for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
            if (file_inf_array[i].isMember("F_FileName") && file_inf_array[i].isMember("F_FilePath")) {
                std::string filepath = file_inf_array[i]["F_FilePath"].asString();
                std::string filename = file_inf_array[i]["F_FileName"].asString();
                auto tag = file_inf_array[i]["F_UploadedTag"].asInt();
                if (tag == 0) {
                    Logger::log(LogLevel::INFO, "uploading file: %s", filename.c_str());
                    auto pathname = filepath + "/" + filename;
                    storage->uploadFile(pathname);
                }
            }
        }

        while (!storage->isUploadFinished()) {
            usleep(1);
        }

        for (auto& filename : uploaded_file_list) {
            for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
                auto pathname = file_inf_array[i]["F_FilePath"].asString() + "/" + file_inf_array[i]["F_FileName"].asString();
                if (pathname == filename) {
                    root["file_inf"][i]["F_UploadedTag"] = 1;
                    // grill 2026-06-28: wm 始终删除已传媒体（capture-upload-forget；不再走
                    // FileManage 配置门——wm 不在 SD 留存已传内容，也止媒体目录膨胀）。
                    Misc::deleteFile(pathname);
                }
            }
        }

        std::ofstream ofs(desc_filename);
        ofs << root.toStyledString();
        ofs.close();
        if (allFileUploaded) {
            Logger::log(LogLevel::INFO, "upload all files finished in %s", desc_filename.c_str());
        }
        // grill 2026-06-28: 整体上传成功（desc 自身 + 全部媒体）→ 删除 desc。wm 是
        // capture-upload-forget：不在 SD 留存已传 desc，也止 upload 目录无界增长拖慢扫描。
        // 部分成功（有媒体未传）保留 desc，下次扫描按 file_inf 的 F_UploadedTag 重传。
        if (descfile_uploaded && allFileUploaded) {
            Misc::deleteFile(desc_filename);
            Logger::log(LogLevel::INFO, "UploadTask: desc removed (upload complete): %s",
                        desc_filename.c_str());
        }
    }
}

}  // namespace app_workmode
