#include "StorageServiceSim.h"

namespace service {

StorageInfo StorageServiceSim::getStorageInfo() {
    StorageInfo info;
    info.total = 32000;
    info.free = 24000;
    info.used = 8000;
    return info;
}

FormatResult StorageServiceSim::formatStorage() {
    FormatResult result;
    result.accepted = true;
    result.status = "scheduled";
    return result;
}

} // namespace service
