#include "StorageServiceT32.h"

#ifndef SIMULATION_MODE

namespace service {

// TODO: Replace with Disk::getInfo("/sdcard")
StorageInfo StorageServiceT32::getStorageInfo() {
    StorageInfo info;
    info.total = 0;
    info.free = 0;
    info.used = 0;
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
