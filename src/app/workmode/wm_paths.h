#pragma once

#include <cstdlib>
#include <string>

namespace app_workmode {

static const char kWmDbPath[] = "/mnt/huntcam/data/db";
static const char kWmMediaRoot[] = "/mnt/huntcam/media";
static const char kWmMediaPath[] = "/mnt/huntcam/media/";
static const char kWmUploadPath[] = "/mnt/huntcam/media/upload/";

inline std::string trimTrailingSlash(const std::string& path) {
    if (path.size() > 1 && path[path.size() - 1] == '/') {
        return path.substr(0, path.size() - 1);
    }
    return path;
}

inline std::string wmSimRoot() {
    const char* root = std::getenv("SIM_SD_ROOT");
    if (root && root[0] != '\0') return trimTrailingSlash(root);
    return "./sim_sdcard_runtime";
}

inline std::string wmDbPath() {
#ifdef BUILD_FOR_SIMULATION
    return wmSimRoot() + "/data/db";
#else
    return kWmDbPath;
#endif
}

inline std::string wmMediaRoot() {
#ifdef BUILD_FOR_SIMULATION
    return wmSimRoot() + "/media";
#else
    return kWmMediaRoot;
#endif
}

inline std::string wmMediaPath() {
    return wmMediaRoot() + "/";
}

inline std::string wmUploadPath() {
    return wmMediaRoot() + "/upload/";
}

}  // namespace app_workmode
