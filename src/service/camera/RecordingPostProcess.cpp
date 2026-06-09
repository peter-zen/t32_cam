#include "RecordingPostProcess.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

#include <elog.h>

#include "../../common/misc/Misc.h"

namespace service {
namespace camera {

namespace {

constexpr const char* kTag = "RecPost";

// 提取 filePath 的父目录(空串表示无父目录)
static std::string parentDirOf(const std::string& filePath) {
    size_t slash = filePath.find_last_of('/');
    return (slash == std::string::npos) ? std::string() : filePath.substr(0, slash);
}

} // namespace

void RecordingPostProcess::writeWorkModeDescJson(const std::string& jsonContent,
                                                  const std::string& filePath) {
    // 1. 父目录创建(项目是 C++14 + GCC 5.4,没有 std::filesystem;
    //    用 Misc::createDirectory,内部已经做了 mkdir -p 递归)
    std::string parent = parentDirOf(filePath);
    if (!parent.empty()) {
        struct stat st;
        bool exists = (stat(parent.c_str(), &st) == 0);
        if (!exists) {
            if (!Misc::createDirectory(parent)) {
                elog_e(kTag, "writeWorkModeDescJson: create dir %s failed: %s",
                       parent.c_str(), std::strerror(errno));
                return;
            }
        }
    }

    // 2. 写文件
    try {
        std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            elog_e(kTag, "writeWorkModeDescJson: open %s failed: %s",
                   filePath.c_str(), std::strerror(errno));
            return;
        }
        ofs.write(jsonContent.data(), static_cast<std::streamsize>(jsonContent.size()));
        ofs.close();
        if (ofs.fail()) {
            elog_e(kTag, "writeWorkModeDescJson: write %s failed", filePath.c_str());
            return;
        }
        elog_i(kTag, "writeWorkModeDescJson: wrote %zu bytes to %s",
               jsonContent.size(), filePath.c_str());
    } catch (const std::exception& e) {
        elog_e(kTag, "writeWorkModeDescJson: write %s threw: %s",
               filePath.c_str(), e.what());
    }
}

} // namespace camera
} // namespace service
