#include "MdnsTxtRecord.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool containsRecord(const std::vector<std::string>& records, const std::string& expected) {
    return std::find(records.begin(), records.end(), expected) != records.end();
}

} // namespace

int main() {
    service::MdnsTxtPayload payload;
    payload.deviceFamily = service::kDefaultMdnsDeviceFamily;
    payload.model = "T32CamPro";
    payload.serialNumber = "ABC123456";
    payload.firmwareVersion = "1.2.3";
    payload.rtspPort = 554;
    payload.ctrlPort = 8080;
    payload.macAddress = "AA:BB:CC:DD:EE:FF";
    payload.status = "ready";

    const std::vector<std::string> records = service::MdnsTxtRecord::build(payload);

    const std::vector<std::string> expectedRecords = {
        "device_family=ckvison_t32cam",
        "model=T32CamPro",
        "sn=ABC123456",
        "fw_ver=1.2.3",
        "rtsp_port=554",
        "ctrl_port=8080",
        "mac=AA:BB:CC:DD:EE:FF",
        "status=ready"
    };

    for (const auto& expected : expectedRecords) {
        if (!containsRecord(records, expected)) {
            std::cerr << "Missing TXT record: " << expected << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (records.size() != expectedRecords.size()) {
        std::cerr << "Unexpected TXT record count: " << records.size() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
