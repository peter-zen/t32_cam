#ifndef MDNS_TXT_RECORD_H
#define MDNS_TXT_RECORD_H

#include <cstdint>
#include <string>
#include <vector>

namespace service {

constexpr const char* kDefaultMdnsDeviceFamily = "ckvison_t32cam";
constexpr uint16_t kDefaultMdnsRtspPort = 554;

struct MdnsTxtPayload {
    std::string deviceFamily = kDefaultMdnsDeviceFamily;
    std::string model;
    std::string serialNumber;
    std::string firmwareVersion;
    uint16_t rtspPort = kDefaultMdnsRtspPort;
    uint16_t ctrlPort = 80;
    std::string macAddress;
    std::string status = "ready";
};

class MdnsTxtRecord {
public:
    static std::vector<std::string> build(const MdnsTxtPayload& payload);
};

} // namespace service

#endif
