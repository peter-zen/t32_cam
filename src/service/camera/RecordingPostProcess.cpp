#include "RecordingPostProcess.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <elog.h>

namespace service {
namespace camera {

namespace {

constexpr const char* kTag = "RecPost";

} // namespace

void RecordingPostProcess::writeWorkModeDescJson(const std::string& jsonContent,
                                                  const std::string& filePath) {
    namespace fs = std::filesystem;

    // 1. 父目录创建
    try {
        fs::path p(filePath);
        fs::path parent = p.parent_path();
        if (!parent.empty() && !fs::exists(parent)) {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (ec) {
                elog_e(kTag, "writeWorkModeDescJson: create dir %s failed: %s",
                       parent.c_str(), ec.message().c_str());
                return;
            }
        }
    } catch (const std::exception& e) {
        elog_e(kTag, "writeWorkModeDescJson: create dir threw: %s", e.what());
        return;
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
