#ifndef STORAGE_SERVICE_SIM_H
#define STORAGE_SERVICE_SIM_H

#include "../IStorageService.h"

namespace service {

class StorageServiceSim : public IStorageService {
public:
    StorageInfo getStorageInfo() override;
    FormatResult formatStorage() override;
};

} // namespace service

#endif // STORAGE_SERVICE_SIM_H
