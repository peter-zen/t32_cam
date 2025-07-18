#include <sys/statvfs.h>
#include "Disk.h"
#include "Logger.h"
#include <cstdint>

DiskInfo Disk::getInfo(const std::string &path)
{
    DiskInfo disk_info;
    disk_info.total = 0;
    disk_info.free = 0;
    if (path.empty()) {
        return disk_info;
    }

    struct statvfs stat;
    if (statvfs(path.c_str(), &stat) != 0) {
        Logger::log(LogLevel::ERROR, "Failed to get disk info");
        return disk_info;
    }

    uint64_t total_bytes = static_cast<uint64_t>(stat.f_blocks) * stat.f_frsize;
    uint64_t free_bytes = static_cast<uint64_t>(stat.f_bavail) * stat.f_frsize;

    // Calculate total and free space in MB
    disk_info.total = static_cast<int>(total_bytes / (1024 * 1024));
    disk_info.free = static_cast<int>(free_bytes / (1024 * 1024));

    Logger::log(LogLevel::INFO, "Total disk space: %d MB", disk_info.total);
    Logger::log(LogLevel::INFO, "Free disk space: %d MB", disk_info.free);
    return disk_info;
}
