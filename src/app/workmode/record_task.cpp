#include "record_task.h"

#include "upload_worker.h"
#include "wm_paths.h"

#include "CameraRecorder.h"        // service::camera::CameraRecorder / RecordOptions / RecordResult / RecordError
#include "MetadataDao.h"           // MetadataDao::saveThumbnail
#include "RecordingPostProcess.h"  // RecordingPostProcess::writeWorkModeDescJson
#include "Manifest.h"              // manifest::generateDescInfo
#include "Common.h"
#include "Logger.h"
#include "misc/Misc.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <malloc.h>
#include <cstdlib>

namespace app_workmode {

namespace {

std::string formatNow() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    std::ostringstream ss;
    ss << std::put_time(&tmv, "%Y%m%d_%H%M%S");
    return ss.str();
}

// 取路径 basename 去扩展名（/a/b/ts.mp4 → ts），让 desc 名与对应媒体同名。
std::string fileStem(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

}  // namespace

RecordTask::RecordTask(std::shared_ptr<UploadWorker> uploadWorker)
    : uploadWorker_(std::move(uploadWorker)),
      recorder_(std::make_shared<service::camera::CameraRecorder>()) {}

RecordTask::~RecordTask() {
    stop();
}

bool RecordTask::trigger() {
    bool expected = false;
    if (!recording_.compare_exchange_strong(expected, true)) {
        Logger::log(LogLevel::INFO, "RecordTask: previous record in progress, skip trigger");
        return false;
    }

    // 主线程释放上一段的 video_recorder_（channel 级 teardown，不 System_Exit）。
    // 必须在新 record() 之前：record() 内会 make_shared 新 VideoRecorder，若旧的不先释放，
    // 新构造会 IMP_Encoder_CreateChn(0) 撞上一段的 channel 0。trigger() 由 EventLoop 主线程
    // 同步调用，此时上一段录影后台线程已退出（onComplete 跑完、recording_ 已 false），
    // ~VideoRecorder→deinitialize→thread.join() join 的是已退出线程，安全（不 self-join）。
    recorder_->releaseVideoResources();

    const std::string mediaPath = wmMediaPath();
    const std::string uploadPath = wmUploadPath();
    if (!Misc::createDirectory(mediaPath) || !Misc::createDirectory(uploadPath)) {
        Logger::log(LogLevel::ERROR, "RecordTask: create media dirs failed target=%s upload=%s",
                    mediaPath.c_str(), uploadPath.c_str());
        recording_ = false;
        return false;
    }

    std::string record_path = mediaPath + formatNow() + ".mp4";

    service::camera::RecordOptions opts;
    opts.audio = false;        // 默认关（当前设备无 audio 硬件）
    opts.autoCover = false;
    if (const char* e = std::getenv("HTC_RECORD_AUDIO")) {
        if (e[0] == '1') opts.audio = true;
    }
    if (const char* e = std::getenv("HTC_RECORD_NO_THUMBNAIL")) {
        if (e[0] == '1') opts.concurrentSnap = false;
    }
    if (const char* e = std::getenv("HTC_RECORD_BITRATE_KBPS")) {
        if (e[0] != '\0') {
            int kbps = std::atoi(e);
            if (kbps > 0) opts.bitrateKbpsOverride = kbps;
        }
    }

    // durationSec=0 → CameraRecorder 读 CPS 配置时长。onComplete 在 VideoRecorder
    // 后台线程执行，不阻塞事件循环。
    opts.onComplete = [this, record_path](const service::camera::RecordResult& r) {
        onCompleteRecord(record_path, r);
    };

    Logger::log(LogLevel::INFO, "RecordTask: record start: %s", record_path.c_str());
    if (!recorder_->record(record_path, 0, opts)) {
        recording_ = false;
        Logger::log(LogLevel::ERROR, "RecordTask: record start failed (rejected)");
        return false;
    }
    return true;
}

void RecordTask::onCompleteRecord(const std::string& record_path,
                                  const service::camera::RecordResult& r) {
    // 1) 缩略图（release 前取）
    if (recorder_->hasThumbnail()) {
        MetadataDao dao;
        if (dao.saveThumbnail(record_path, recorder_->getThumbnailData())) {
            Logger::log(LogLevel::INFO, "RecordTask: thumbnail saved for %s (%zu bytes)",
                        record_path.c_str(), recorder_->getThumbnailData().size());
        } else {
            Logger::log(LogLevel::ERROR, "RecordTask: saveThumbnail failed for %s", record_path.c_str());
        }
    }

    // 2) SDK 帧缓冲释放【不能在此处做】：onCompleteRecord 跑在 VideoRecorder 的异步
    //    线程里（onRecordDone 回调），若调 releaseVideoResources→~VideoRecorder→
    //    deinitialize→thread.join()，会 join 自己正在跑的这个线程 = self-join →
    //    "Resource deadlock avoided" 崩溃。释放改到主线程：下次 trigger 的 make_shared
    //    旧 video_recorder_ 析构，或 stop()。缩略图须在释放前取（上面已取）。

    // 3) desc 落盘（仅成功完成/主动停）。uploadWorker_ 非空时 enqueue（legacy）；
    //    wm 路径传 nullptr，desc 只落盘——由 type 2 UploadTask 自扫上传。
    if (r.error == service::camera::RecordError::None ||
        r.error == service::camera::RecordError::UserStop) {
        completedCount_.fetch_add(1);  // 供 EventLoop 的 HTC_TEST_RECORD_COUNT 门控判定
        std::vector<std::string> files = { record_path };
        std::string desc_info;
        if (manifest::generateDescInfo(files, desc_info) == 0) {
            std::string desc_filename = wmUploadPath() + fileStem(record_path) + ".json";
            service::camera::RecordingPostProcess::writeWorkModeDescJson(desc_info, desc_filename);
            if (uploadWorker_) {
                uploadWorker_->enqueue(desc_filename);
                Logger::log(LogLevel::INFO, "RecordTask: desc enqueued: %s", desc_filename.c_str());
            } else {
                Logger::log(LogLevel::INFO, "RecordTask: desc written: %s", desc_filename.c_str());
            }
        } else {
            Logger::log(LogLevel::ERROR, "RecordTask: generateDescInfo failed");
        }
    } else {
        Logger::log(LogLevel::ERROR, "RecordTask: record failed: %s", r.errorMessage.c_str());
    }

    recording_ = false;  // 允许下次 trigger
}

void RecordTask::stop() {
    if (recording_.load()) {
        Logger::log(LogLevel::INFO, "RecordTask: stopping current record...");
        recorder_->stop();
        // 等 onComplete（最多 8s）
        for (int i = 0; i < 80 && recording_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (recording_.load()) {
            Logger::log(LogLevel::WARNING, "RecordTask: stop wait timed out, forcing release");
            recording_ = false;
        }
    }
    // 主线程释放 SDK 帧缓冲（64MB zram 防护）。此处不在异步线程内，~VideoRecorder→
    // deinitialize join 的是已跑完的异步线程，安全（不会 self-join）。
    // controlISP 已从 cleanupHook 移除，releaseVideoResources → IngenicVideo::exit() 按
    // 官方序列(imp_isp.h:74-102 / imp_system.h:129-131)teardown，不触发 defog ISR 解引用。
    // 无条件跑——跳过会留 encoder channel → 下段录影 IMP_Encoder_CreateChn(0) 失败。
    ::sync();
    recorder_->releaseVideoResources();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ::malloc_trim(0);
}

}  // namespace app_workmode
