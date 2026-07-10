// wm_ingest — m2 lean upload：为 quickSnap 工作目录构建/复用上传 desc。
// 见 wm_ingest.h 与 doc/design/workmode-m2-workunit-handoff.md。

#include "wm_ingest.h"

#include "Manifest.h"        // manifest::createDescInfoFile
#include "misc/Misc.h"       // listFilenames / getFilename
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <unistd.h>          // access（desc 存在探测）

namespace app_workmode {

namespace {

// 媒体扩展名白名单（大小写不敏感）。quickSnap 写大写 .JPG；视频 .mp4。
bool isAllowedMedia(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string ext = filename.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == "jpg" || ext == "jpeg" || ext == "mp4";
}

// workDir 可能带尾 '/'，basename 取最后一段（去尾斜杠后）。
std::string dirBasename(const std::string& workDir) {
    std::string d = workDir;
    while (!d.empty() && d.back() == '/') d.pop_back();
    return Misc::getFilename(d);
}

}  // namespace

int ensureWorkDirDesc(const std::string& workDir, std::string* descPathOut) {
    const std::string name = dirBasename(workDir);
    if (name.empty()) {
        Logger::log(LogLevel::WARNING, "[wm] ensureWorkDirDesc: empty dir name (%s)", workDir.c_str());
        return 0;
    }

    // 扫描目录（保证尾斜杠：listFilenames + scanDir + 文件名 拼接用）
    std::string scanDir = workDir;
    if (!scanDir.empty() && scanDir.back() != '/') scanDir += '/';

    // desc 路径 = <workDir 去尾斜杠>/<basename>.json
    std::string dir = scanDir;
    if (!dir.empty() && dir.back() == '/') dir.pop_back();
    const std::string descPath = dir + "/" + name + ".json";

    // desc 已存在 → 复用（断点续传，保 F_UploadedTag，不重建、不读传感器）
    if (access(descPath.c_str(), F_OK) == 0) {
        Logger::log(LogLevel::INFO, "[wm] desc reuse (resume): %s", descPath.c_str());
        if (descPathOut) *descPathOut = descPath;
        return 1;
    }

    // desc 不存在 → 扫白名单媒体建 desc
    std::vector<std::string> media_paths;   // createDescInfoFile 取非 const 引用
    std::vector<std::string> files = Misc::listFilenames(scanDir);
    for (const std::string& f : files) {
        if (isAllowedMedia(f)) {
            media_paths.push_back(scanDir + f);
        }
    }
    if (media_paths.empty()) {
        Logger::log(LogLevel::INFO, "[wm] no media in %s, skip desc", workDir.c_str());
        return 0;
    }

    if (manifest::createDescInfoFile(media_paths, descPath) != 0) {
        Logger::log(LogLevel::ERROR, "[wm] createDescInfoFile failed for %s", descPath.c_str());
        return -1;
    }
    Logger::log(LogLevel::INFO, "[wm] ingest %zu media -> %s", media_paths.size(), descPath.c_str());
    if (descPathOut) *descPathOut = descPath;
    return 1;
}

}  // namespace app_workmode
