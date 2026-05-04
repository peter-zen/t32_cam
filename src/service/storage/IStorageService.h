#ifndef I_STORAGE_SERVICE_H
#define I_STORAGE_SERVICE_H

#include "../ServiceTypes.h"

namespace service {

class IStorageService {
public:
    virtual ~IStorageService() = default;
    virtual StorageInfo getStorageInfo() = 0;
    virtual FormatResult formatStorage() = 0;
};

} // namespace service

#endif // I_STORAGE_SERVICE_H
