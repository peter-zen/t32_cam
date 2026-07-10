// SnapTask — see snap_task.h. Synchronous photo capture (ImageSnap::snap) +
// thumbnail persist + upload desc, mirroring the CameraServiceT32::takePhoto +
// processCmdDesc sequence but standalone.

#include "snap_task.h"

#include "upload_worker.h"
#include "wm_paths.h"

#include "ImageSnap.h"          // media::ImageSnap / ImageSnapParams
#include "MetadataDao.h"        // MetadataDao::saveThumbnail
#include "StoragePaths.h"        // storage::StoragePaths::makeMediaName
#include "Manifest.h"           // manifest::createDescInfoFile
#include "Settings.h"           // Settings::burstNumber / stillSize
#include "Common.h"             // SnapImgSize[], SNAP_IMG_SIZE_*
#include "StringConvert.h"      // to_string_custom
#include "Logger.h"
#include "misc/Misc.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace app_workmode {

namespace {
// 取路径 basename 去扩展名（/a/b/ts_1.jpg → ts_1），让 desc 名与对应媒体同名。
std::string fileStem(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

// cm==1 diag: memory snapshot at photo phase boundaries (mirrors the identical
// logMemInfo in VideoRecorder.cpp so the photo→record trajectory is comparable).
// See there for field semantics. Remove after root-caused.
static void logMemInfo(const char* tag) {
    long memFree = -1, memAvail = -1, swapFree = -1, swapCached = -1;
    if (FILE* f = fopen("/proc/meminfo", "r")) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "MemFree:", 8)) memFree = atol(line + 9);
            else if (!strncmp(line, "MemAvailable:", 13)) memAvail = atol(line + 14);
            else if (!strncmp(line, "SwapFree:", 9)) swapFree = atol(line + 10);
            else if (!strncmp(line, "SwapCached:", 11)) swapCached = atol(line + 12);
        }
        fclose(f);
    }
    long vmSize = -1, vmRSS = -1, vmSwap = -1;
    if (FILE* f = fopen("/proc/self/status", "r")) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (!strncmp(line, "VmSize:", 7)) vmSize = atol(line + 7);
            else if (!strncmp(line, "VmRSS:", 6)) vmRSS = atol(line + 6);
            else if (!strncmp(line, "VmSwap:", 7)) vmSwap = atol(line + 7);
        }
        fclose(f);
    }
    Logger::log(LogLevel::INFO,
        "MEMINFO[%s] MemFree=%ld MemAvail=%ld SwapFree=%ld SwapCached=%ld kB | "
        "VmSize=%ld VmRSS=%ld VmSwap=%ld kB",
        tag, memFree, memAvail, swapFree, swapCached, vmSize, vmRSS, vmSwap);
    if (FILE* f = fopen("/proc/buddyinfo", "r")) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
            Logger::log(LogLevel::INFO, "BUDDY[%s] %s", tag, line);
        }
        fclose(f);
    }
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

    const std::time_t now = std::time(nullptr);
    const std::string mediaPath = wmMediaPath();
    const std::string uploadPath = wmUploadPath();
    if (!Misc::createDirectory(mediaPath) || !Misc::createDirectory(uploadPath)) {
        Logger::log(LogLevel::ERROR, "SnapTask: create media dirs failed target=%s upload=%s",
                    mediaPath.c_str(), uploadPath.c_str());
        return false;
    }

    std::vector<std::string> files;
    for (int i = 0; i < burst; ++i) {
        files.push_back(mediaPath + storage::StoragePaths::makeMediaName(storage::MediaKind::Image, now, i + 1));
    }

    Logger::log(LogLevel::INFO, "SnapTask: snap start %dx%d burst=%d first=%s",
                W, H, burst, files[0].c_str());
    logMemInfo("photo-entry");

    bool ok = false;
    {
        media::ImageSnapParams params;
        params.setImageSize(W, H);
        params.setSensorNativeSize(2560, 1440);   // sensor-native; >sensor 走软件缩放
        params.setThumbnailEnabled(true);          // CH2 缩略图
        media::ImageSnap snap(params);

        // snap() 写 JPG 文件（addMedia 已从 ImageSnap 移出，归还调用方）；缩略图捕获进 thumbData_ 但不落盘。
        ok = snap.snap(files);
        logMemInfo("photo-captured");

        if (ok && snap.hasThumbnail() && !files.empty()) {
            MetadataDao dao;
            if (dao.saveThumbnail(files[0], snap.getThumbnailData())) {
                Logger::log(LogLevel::INFO, "SnapTask: thumbnail saved for %s (%zu bytes)",
                            files[0].c_str(), snap.getThumbnailData().size());
            } else {
                Logger::log(LogLevel::ERROR, "SnapTask: saveThumbnail failed for %s", files[0].c_str());
            }
        }

        // 入库：per-file 写 media_file.db（addMedia 从 ImageSnap 移出后由调用方负责）。
        if (ok) {
            MetadataDao dao;
            for (const auto& f : files) {
                struct stat st;
                long sz = (::stat(f.c_str(), &st) == 0) ? (long)st.st_size : 0;
                MediaItem item;
                item.filePath = f;
                item.type = 1;  /* Photo */
                item.timestamp = std::time(nullptr);
                item.fileSize = sz;
                item.width = W;
                item.height = H;
                if (!dao.addMedia(item)) {
                    Logger::log(LogLevel::WARNING, "SnapTask: addMedia failed for %s", f.c_str());
                }
            }
        }
    }  // Ensure JPEG/thumbnail streams are stopped and destroyed before upload starts.

    if (ok) {
        std::vector<std::string> descFiles = files;   // createDescInfoFile 取非 const 引用
        std::string desc = uploadPath + fileStem(files[0]) + ".json";
        if (manifest::createDescInfoFile(descFiles, desc) == 0) {
            // uploadWorker_ 非空时 enqueue（legacy）；wm 传 nullptr，desc 只落盘——
            // 由 type 2 UploadTask 自扫此目录上传。desc 落盘即完成 type 1 产出职责。
            if (uploadWorker_ && !std::getenv("HTC_NO_UPLOAD")) {
                uploadWorker_->enqueue(desc);
                Logger::log(LogLevel::INFO, "SnapTask: desc enqueued: %s", desc.c_str());
            } else {
                Logger::log(LogLevel::INFO, "SnapTask: desc written: %s", desc.c_str());
            }
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
