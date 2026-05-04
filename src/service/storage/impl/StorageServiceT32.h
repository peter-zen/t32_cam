#ifndef STORAGE_SERVICE_T32_H
#define STORAGE_SERVICE_T32_H

#include "../IStorageService.h"

namespace service {

class StorageServiceT32 : public IStorageService {
public:
    StorageInfo getStorageInfo() override;
    FormatResult formatStorage() override;
};

} // namespace service

#endif // STORAGE_SERVICE_T32_H
