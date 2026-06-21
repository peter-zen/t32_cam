#include "upload_worker.h"

#include "MgmtServClient.h"
#include "StorageServClient.h"
#include "DeviceConfig.h"
#include "Common.h"
#include "misc/Misc.h"
#include "Logger.h"
#include "app.h"

#include <json/json.h>
#include <fstream>
#include <vector>
#include <chrono>
#include <unistd.h>

using namespace network;

namespace app_workmode {

UploadWorker::UploadWorker() = default;

UploadWorker::~UploadWorker() {
    stop();
}

void UploadWorker::start(const std::string& mgmtAddr, int mgmtPort) {
    if (started_) return;
    // 只存配置，不 connect —— 真正 connect/auth 延迟到 worker 线程第一次拿到 desc
    // （即录影完成后），避免启动时就做 DNS/socket/auth 拖慢录影启动。
    mgmtAddr_ = mgmtAddr;
    mgmtPort_ = mgmtPort;
    started_ = true;
    worker_ = std::thread(&UploadWorker::workerLoop, this);
}

void UploadWorker::ensureConnected() {
    if (connected_) return;
    mgmt_ = std::make_shared<MgmtServClient>(mgmtAddr_, mgmtPort_);
    if (EC_SUCCESS != mgmt_->connect(3000)) {
        Logger::log(LogLevel::ERROR, "UploadWorker: connect [%s:%d] failed", mgmtAddr_.c_str(), mgmtPort_);
        return;
    }
    if (EC_SUCCESS != mgmt_->authenticate()) {
        Logger::log(LogLevel::ERROR, "UploadWorker: auth failed");
        return;
    }
    storage_ = mgmt_->newStorageServClient();
    connected_ = true;
    Logger::log(LogLevel::INFO, "UploadWorker: connected + authed [%s:%d] (lazy, after first record)",
                mgmtAddr_.c_str(), mgmtPort_);
}

void UploadWorker::enqueue(const std::string& descPath) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty() && !busy_) {  // 从空闲进入工作：标记本批起始时刻
            firstEnqueueTime_ = std::chrono::steady_clock::now();
        }
        queue_.push_back(descPath);
    }
    cv_.notify_one();
}

bool UploadWorker::isIdle() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty() && !busy_;
}

int64_t UploadWorker::firstEnqueueAgeMs() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty() && !busy_) return 0;  // 完全空闲（在队+在途皆无）
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - firstEnqueueTime_).count();
}

bool UploadWorker::flush(int timeoutMs) {
    std::unique_lock<std::mutex> lock(mutex_);
    // 等到队列排空 且 无在途上传（worker 已 pop 的那个也传完）才算真正 flush 完成。
    return cv_.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                        [this]{ return queue_.empty() && !busy_; });
}

void UploadWorker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void UploadWorker::workerLoop() {
    while (true) {
        std::string descPath;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]{ return stopRequested_ || !queue_.empty(); });
            if (stopRequested_ && queue_.empty()) break;
            if (queue_.empty()) continue;
            descPath = queue_.front();
            queue_.pop_front();
            busy_ = true;  // 标记在途：上传未完成前 isIdle() 返回 false（同锁内设置，无空窗）
        }

        if (!connected_) {
            ensureConnected();  // lazy：第一次拿到 desc（录影完成后）才 connect/auth
        }

        uploadOneDesc(descPath);

        // 在途结束：清 busy_，唤醒 flush()/EventLoop 等待排空的调用方。
        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
        }
        cv_.notify_all();
    }
}

void UploadWorker::uploadOneDesc(const std::string& desc_filename) {
    // 平移自 WorkModeRunner.cpp CMD_UPLOAD 的 per-desc 循环体（原 :742-855）。
    if (!storage_) {
        Logger::log(LogLevel::WARNING, "UploadWorker: no storage client (mgmt not authed?), skip %s",
                    desc_filename.c_str());
        return;
    }
    Logger::log(LogLevel::INFO, "UploadWorker: desc_filename %s", desc_filename.c_str());
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
        storage_->bindUploadCallback([&descfile_uploaded, target_filename](const std::string &filename, int error_code) {
            Logger::log(LogLevel::INFO, "upload descfile [%s], error code: %d", filename.c_str(), error_code);
            if (filename == target_filename) {
                descfile_uploaded = (error_code == EC_SUCCESS) ? true : false;
            }
        });
        storage_->uploadFile(desc_filename);
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

    if (descfile_uploaded) {
        bool allFileUploaded = true;
        const Json::Value file_inf_array = root["file_inf"];
        std::vector<std::string> uploaded_file_list;
        storage_->bindUploadCallback([&uploaded_file_list, &allFileUploaded](const std::string &filename, int error_code) {
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
                    storage_->uploadFile(pathname);
                }
            }
        }

        while (!storage_->isUploadFinished()) {
            usleep(1);
        }

        for (auto& filename : uploaded_file_list) {
            for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
                auto pathname = file_inf_array[i]["F_FilePath"].asString() + "/" + file_inf_array[i]["F_FileName"].asString();
                if (pathname == filename) {
                    root["file_inf"][i]["F_UploadedTag"] = 1;
                    auto file_manage_type = DeviceConfig::getInstance()->get(INI_SECTION_POLICY, INI_KEY_FILE_MANAGE, 0);
                    if (file_manage_type == FILE_MANAGE_DELETE) {
                        Misc::deleteFile(pathname);
                    }
                }
            }
        }

        std::ofstream ofs(desc_filename);
        ofs << root.toStyledString();
        ofs.close();
        if (allFileUploaded) {
            Logger::log(LogLevel::INFO, "upload all files finished in %s", desc_filename.c_str());
        }
    }
}

}  // namespace app_workmode
