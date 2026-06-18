#include "MdnsParams.h"

#include "MdnsService.h"
#include "MdnsTxtRecord.h"
#include "DeviceConfig.h"
#include "misc/Misc.h"
#include "Common.h"
#include "Logger.h"

#include <cctype>
#include <string>

namespace service {

namespace {

// NOTE: this is a verbatim copy of the file-local helper that still lives in
// main_app.cpp (used there by the media-scanner mode parser). Two TUs hold
// identical file-local copies by design — keeps this move byte-identical.
static std::string trimConfigString(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    std::string trimmed = value.substr(start, end - start);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

static std::string getDefaultMdnsInstanceName(const std::shared_ptr<DeviceConfig>& config)
{
    std::string instance_name = trimConfigString(config->get(INI_SECTION_MDNS, INI_KEY_MDNS_INSTANCE_NAME, ""));
    if (!instance_name.empty()) {
        return instance_name;
    }

    instance_name = trimConfigString(config->get(INI_SECTION_BOOT, INI_KEY_PNAME, ""));
    if (!instance_name.empty()) {
        return instance_name;
    }

    instance_name = trimConfigString(config->get(INI_SECTION_DEVICE, INI_KEY_PID, ""));
    if (!instance_name.empty()) {
        return instance_name;
    }

    return "T32Camera";
}

static std::string getDefaultMdnsHostName(const std::shared_ptr<DeviceConfig>& config)
{
    std::string host_name = trimConfigString(config->get(INI_SECTION_MDNS, INI_KEY_MDNS_HOST_NAME, ""));
    if (!host_name.empty()) {
        return host_name;
    }

    host_name = trimConfigString(config->get(INI_SECTION_DEVICE, INI_KEY_PID, ""));
    if (!host_name.empty()) {
        return host_name;
    }

    return "t32cam";
}

} // namespace

MdnsServiceParams buildMdnsParams(const std::shared_ptr<DeviceConfig>& config,
                                  const std::string& interface_name,
                                  const std::string& ip_address,
                                  uint16_t ctrl_port,
                                  uint16_t rtsp_port)
{
    MdnsServiceParams params;
    params.interfaceName = interface_name;
    params.ipAddress = ip_address;
    params.serviceType = trimConfigString(
        config->get(INI_SECTION_MDNS, INI_KEY_MDNS_SERVICE_TYPE, "_t32cam._tcp"));
    params.instanceName = getDefaultMdnsInstanceName(config);
    params.hostName = getDefaultMdnsHostName(config);
    params.txt.deviceFamily = kDefaultMdnsDeviceFamily;
    params.txt.model = trimConfigString(config->get(INI_SECTION_BOOT, INI_KEY_PMODEL, "T32"));
    params.txt.serialNumber = trimConfigString(config->get(INI_SECTION_DEVICE, INI_KEY_PID, ""));
    params.txt.firmwareVersion = CAMERA_VERSION;
    params.txt.rtspPort = rtsp_port;
    params.txt.ctrlPort = ctrl_port;
    params.txt.macAddress = Misc::getMACAddress(interface_name);
    params.txt.status = "ready";
    return params;
}

bool isMdnsEnabled(const std::shared_ptr<DeviceConfig>& config)
{
    return config->get(INI_SECTION_MDNS, INI_KEY_MDNS_ENABLE, 1) != 0;
}

} // namespace service
