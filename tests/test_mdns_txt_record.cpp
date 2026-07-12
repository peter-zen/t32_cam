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
    // ---- existing base case (no caps) ----
    service::MdnsTxtPayload payload;
    payload.deviceFamily = service::kDefaultMdnsDeviceFamily;
    payload.model = "T32CamPro";
    payload.serialNumber = "ABC123456";
    payload.firmwareVersion = "1.2.3";
    payload.rtspPort = 8554;
    payload.ctrlPort = 8080;
    payload.macAddress = "AA:BB:CC:DD:EE:FF";
    payload.status = "ready";
    // M2 — empty caps omitted (appendIfNotEmpty semantics)
    payload.caps = "";

    const std::vector<std::string> records = service::MdnsTxtRecord::build(payload);

    const std::vector<std::string> expectedRecords = {
        "device_family=ckvison_t32cam",
        "model=T32CamPro",
        "sn=ABC123456",
        "fw_ver=1.2.3",
        "rtsp_port=8554",
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
        std::cerr << "Unexpected TXT record count (M2): " << records.size() << std::endl;
        return EXIT_FAILURE;
    }

    // M2 — empty caps must NOT produce a caps= record
    if (containsRecord(records, "caps=") ||
        std::find_if(records.begin(), records.end(),
                     [](const std::string& r) { return r.find("caps=") == 0; }) != records.end()) {
        std::cerr << "M2 FAIL: empty caps produced a caps= record" << std::endl;
        return EXIT_FAILURE;
    }

    // ---- M1 — caps present: serialized as caps=<comma-joined> ----
    service::MdnsTxtPayload capsPayload = payload;
    capsPayload.caps = "um_live";

    const std::vector<std::string> capsRecords = service::MdnsTxtRecord::build(capsPayload);
    if (!containsRecord(capsRecords, "caps=um_live")) {
        std::cerr << "M1 FAIL: missing caps=um_live record" << std::endl;
        return EXIT_FAILURE;
    }

    // multi-token caps
    service::MdnsTxtPayload multiPayload = payload;
    multiPayload.caps = "um_live,um_pb";
    const std::vector<std::string> multiRecords = service::MdnsTxtRecord::build(multiPayload);
    if (!containsRecord(multiRecords, "caps=um_live,um_pb")) {
        std::cerr << "M1 FAIL: missing caps=um_live,um_pb record" << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
