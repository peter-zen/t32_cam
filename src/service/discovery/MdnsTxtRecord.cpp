#include "MdnsTxtRecord.h"

#include <sstream>

namespace service {

namespace {

void appendIfNotEmpty(std::vector<std::string>& records, const std::string& key, const std::string& value) {
    if (!value.empty()) {
        records.push_back(key + "=" + value);
    }
}

std::string portToString(uint16_t port) {
    std::ostringstream oss;
    oss << port;
    return oss.str();
}

} // namespace

std::vector<std::string> MdnsTxtRecord::build(const MdnsTxtPayload& payload) {
    std::vector<std::string> records;
    records.reserve(9);

    appendIfNotEmpty(records, "device_family", payload.deviceFamily);
    appendIfNotEmpty(records, "model", payload.model);
    appendIfNotEmpty(records, "sn", payload.serialNumber);
    appendIfNotEmpty(records, "fw_ver", payload.firmwareVersion);
    appendIfNotEmpty(records, "rtsp_port", portToString(payload.rtspPort));
    appendIfNotEmpty(records, "ctrl_port", portToString(payload.ctrlPort));
    appendIfNotEmpty(records, "mac", payload.macAddress);
    appendIfNotEmpty(records, "status", payload.status);
    appendIfNotEmpty(records, "caps", payload.caps);

    return records;
}

} // namespace service
