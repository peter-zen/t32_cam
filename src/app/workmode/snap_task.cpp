// SnapTask — see snap_task.h. Synchronous photo capture (ImageSnap::snap) +
// thumbnail persist + upload desc, mirroring the CameraServiceT32::takePhoto +
// processCmdDesc sequence but standalone.

#include "snap_task.h"

#include "upload_worker.h"

#include "ImageSnap.h"          // media::ImageSnap / ImageSnapParams
#include "MetadataDao.h"        // MetadataDao::saveThumbnail
#include "Manifest.h"           // manifest::createDescInfoFile
#include "Settings.h"           // Settings::burstNumber / stillSize
// Common.h MUST precede app.h (app.h opens extern "C" then includes Common.h).
#include "Common.h"             // SnapImgSize[], SNAP_IMG_SIZE_*
#include "app.h"                // MEDIA_TARGET_PATH / MEDIA_UPLOAD_PATH
#include "StringConvert.h"      // to_string_custom
#include "Logger.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

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
}  // namespace

SnapTask::SnapTask(std::shared_ptr<UploadWorker> uploadWorker)
    : uploadWorker_(std::move(uploadWorker)) {}

SnapTask::~SnapTask() = default;

bool SnapTask::trigger() {
    int burst = Settings::getInstance()->burstNumber;
    if (burst <= 0) burst = 1;
    int idx = Settings::getInstance()->stillSize;
    if (idx >= SNAP_IMG_SIZE_MAX) idx = SNAP_IMG_SIZE_4M;   // safe default (4M 2560x1440)
    const int W = SnapImgSize[idx].width;
    const int H = SnapImgSize[idx].height;

    const std::string ts = formatNow();
    std::vector<std::string> files;
    for (int i = 0; i < burst; ++i) {
        files.push_back(std::string(MEDIA_TARGET_PATH) + ts + "_" + to_string_custom(i + 1) + ".jpg");
    }

    media::ImageSnapParams params;
    params.setImageSize(W, H);
    params.setSensorNativeSize(2560, 1440);   // sensor-native; >sensor 走软件缩放
    params.setThumbnailEnabled(true);          // CH2 缩略图
    media::ImageSnap snap(params);

    Logger::log(LogLevel::INFO, "SnapTask: snap start %dx%d burst=%d first=%s",
                W, H, burst, files[0].c_str());
    // snap() 写 JPG 文件 + 内部 addMedia(type=1 Photo)；缩略图捕获进 thumbData_ 但不落盘。
    bool ok = snap.snap(files);

    if (ok && snap.hasThumbnail() && !files.empty()) {
        MetadataDao dao;
        if (dao.saveThumbnail(files[0], snap.getThumbnailData())) {
            Logger::log(LogLevel::INFO, "SnapTask: thumbnail saved for %s (%zu bytes)",
                        files[0].c_str(), snap.getThumbnailData().size());
        } else {
            Logger::log(LogLevel::ERROR, "SnapTask: saveThumbnail failed for %s", files[0].c_str());
        }
    }

    if (ok) {
        std::vector<std::string> descFiles = files;   // createDescInfoFile 取非 const 引用
        std::string desc = std::string(MEDIA_UPLOAD_PATH) + ts + ".json";
        if (manifest::createDescInfoFile(descFiles, desc) == 0) {
            if (uploadWorker_) uploadWorker_->enqueue(desc);
            Logger::log(LogLevel::INFO, "SnapTask: desc enqueued: %s", desc.c_str());
        } else {
            Logger::log(LogLevel::ERROR, "SnapTask: createDescInfoFile failed");
            ok = false;
        }
    } else {
        Logger::log(LogLevel::ERROR, "SnapTask: snap failed");
    }
    return ok;
}

}  // namespace app_workmode
