#ifndef MDNS_SERVICE_H
#define MDNS_SERVICE_H

#include "MdnsTxtRecord.h"

#include <memory>
#include <mutex>
#include <string>

struct mdnsd;

namespace service {

std::string normalizeMdnsModelValue(const std::string& model);

struct MdnsServiceParams {
    std::string interfaceName;
    std::string ipAddress;
    std::string serviceType = "_t32cam._tcp";
    std::string instanceName;
    std::string hostName;
    MdnsTxtPayload txt;
};

class MdnsService {
public:
    static std::shared_ptr<MdnsService> getInstance();

    bool start(const MdnsServiceParams& params);
    bool refresh(const MdnsServiceParams& params);
    bool updateStatus(const std::string& status);
    void stop();
    bool isRunning() const;

private:
    MdnsService() = default;

    bool startLocked(const MdnsServiceParams& params);
    void stopLocked();
    MdnsServiceParams normalizeParams(const MdnsServiceParams& params) const;
    bool registerServiceLocked();

    mutable std::mutex mutex_;
    struct mdnsd* server_ = nullptr;
    MdnsServiceParams params_;
    bool running_ = false;
};

} // namespace service

#endif
