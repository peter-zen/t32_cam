#include "StorageServiceT32.h"

#ifndef SIMULATION_MODE

#include <sys/statvfs.h>

namespace service {

StorageInfo StorageServiceT32::getStorageInfo() {
    StorageInfo info;
    struct statvfs stat;
    if (statvfs("/sdcard", &stat) == 0) {
        unsigned long long blockSize = stat.f_frsize ? stat.f_frsize : stat.f_bsize;
        info.total = static_cast<long long>((stat.f_blocks * blockSize) >> 20);
        info.free = static_cast<long long>((stat.f_bavail * blockSize) >> 20);
        info.used = info.total - info.free;
    }
    return info;
}

// TODO: Implement real format operation
FormatResult StorageServiceT32::formatStorage() {
    FormatResult result;
    result.accepted = true;
    result.status = "pending";
    return result;
}

} // namespace service

#endif // SIMULATION_MODE
