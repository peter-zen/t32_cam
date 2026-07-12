#ifndef MDNS_TXT_RECORD_H
#define MDNS_TXT_RECORD_H

#include <cstdint>
#include <string>
#include <vector>

namespace service {

constexpr const char* kDefaultMdnsDeviceFamily = "ckvison_t32cam";
constexpr uint16_t kDefaultMdnsRtspPort = 8554;

struct MdnsTxtPayload {
    std::string deviceFamily = kDefaultMdnsDeviceFamily;
    std::string model;
    std::string serialNumber;
    std::string firmwareVersion;
    uint16_t rtspPort = kDefaultMdnsRtspPort;
    uint16_t ctrlPort = 80;
    std::string macAddress;
    std::string status = "ready";
    std::string caps;  // T28: um_* capability presence-set, comma-joined ("um_live,um_pb")
};

class MdnsTxtRecord {
public:
    static std::vector<std::string> build(const MdnsTxtPayload& payload);
};

} // namespace service

#endif
