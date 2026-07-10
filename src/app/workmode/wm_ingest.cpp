// wm_ingest — m2 lean upload manifest ingest (wm-app-spec §2.1).
// Reads /tmp/media/info.json → builds desc via manifest::createDescInfoFile
// → writes to scan directory picked up by UploadTask.

#include "wm_ingest.h"

#include "Manifest.h"        // manifest::createDescInfoFile
#include "app.h"             // QUICK_SNAP_INFO_FILE, QUICK_SNAP_DIR
#include "misc/Misc.h"       // createDirectory
#include "Logger.h"

#include <json/json.h>
#include <fstream>
#include <vector>
#include <string>

namespace app_workmode {

int ingestQuickSnapManifest(const std::string& scanDir) {
    std::ifstream ifs(QUICK_SNAP_INFO_FILE);  // "/tmp/media/info.json"
    if (!ifs.is_open()) {
        Logger::log(LogLevel::INFO, "[wm] no quickSnap manifest %s (m2 nothing to upload)",
                    QUICK_SNAP_INFO_FILE);
        return 0;
    }

    Json::Value root;
    Json::CharReaderBuilder rb;
    std::string errs;
    if (!Json::parseFromStream(rb, ifs, &root, &errs)) {
        Logger::log(LogLevel::WARNING, "[wm] manifest parse failed: %s", errs.c_str());
        return -1;
    }

    const std::string dir = root["dir"].asString();          // e.g. "20260709_120000"
    const Json::Value& files = root["files"];                // ["20260709_120000_1.jpg", ...]
    if (dir.empty() || !files.isArray() || files.empty()) {
        Logger::log(LogLevel::INFO, "[wm] manifest empty, skip");
        return 0;
    }

    Misc::createDirectory(scanDir);

    std::vector<std::string> media_paths;   // createDescInfoFile takes non-const ref
    media_paths.reserve(files.size());
    for (const auto& f : files) {
        // Full path = /tmp/media/<dir>/<file>. createDescInfoFile internally
        // uses Misc::getFilepath / getFilename to split into F_FilePath and
        // F_FileName, so we just feed the complete path.
        media_paths.push_back(std::string(QUICK_SNAP_DIR) + dir + "/" + f.asString());
    }

    const std::string descPath = scanDir + "/" + dir + ".json";
    if (manifest::createDescInfoFile(media_paths, descPath) == 0) {
        Logger::log(LogLevel::INFO, "[wm] ingest %d files -> %s",
                    static_cast<int>(media_paths.size()), descPath.c_str());
        return 1;
    }

    Logger::log(LogLevel::ERROR, "[wm] ingest createDescInfoFile failed for %s",
                descPath.c_str());
    return -2;
}

}  // namespace app_workmode
