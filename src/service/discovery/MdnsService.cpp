#include "MdnsService.h"

#include <elog.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <vector>

extern "C" {
#include "mdnsd.h"
}

namespace service {

namespace {

constexpr const char* kTag = "MDNS";
constexpr const char* kDefaultServiceType = "_t32cam._tcp.local";
constexpr const char* kDefaultInstanceName = "T32Camera";
constexpr const char* kDefaultHostName = "t32cam.local";

std::string trimWhitespace(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

std::string trimQuotes(const std::string& value) {
    std::string trimmed = trimWhitespace(value);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

std::string normalizeServiceType(const std::string& serviceType) {
    std::string normalized = trimQuotes(serviceType);
    if (normalized.empty()) {
        return kDefaultServiceType;
    }

    if (normalized.find(".local") == std::string::npos) {
        normalized += ".local";
    }
    return normalized;
}

std::string normalizeInstanceName(const std::string& instanceName) {
    std::string normalized = trimQuotes(instanceName);
    if (normalized.empty()) {
        return kDefaultInstanceName;
    }

    std::replace_if(normalized.begin(), normalized.end(), [](char c) {
        return std::iscntrl(static_cast<unsigned char>(c)) != 0;
    }, '-');
    return normalized;
}

std::string normalizeHostName(const std::string& hostName) {
    std::string normalized = trimQuotes(hostName);
    if (normalized.empty()) {
        normalized = kDefaultHostName;
    }

    std::replace_if(normalized.begin(), normalized.end(), [](char c) {
        return c == ' ';
    }, '-');

    std::string sanitized;
    sanitized.reserve(normalized.size());
    for (char c : normalized) {
        const unsigned char ch = static_cast<unsigned char>(c);
        if (std::isalnum(ch) != 0) {
            sanitized.push_back(static_cast<char>(std::tolower(ch)));
        } else if (c == '-' || c == '.') {
            sanitized.push_back(c);
        } else {
            sanitized.push_back('-');
        }
    }

    while (!sanitized.empty() && (sanitized.front() == '-' || sanitized.front() == '.')) {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && (sanitized.back() == '-' || sanitized.back() == '.')) {
        sanitized.pop_back();
    }

    if (sanitized.empty()) {
        sanitized = "t32cam";
    }

    if (sanitized.find(".local") == std::string::npos) {
        sanitized += ".local";
    }

    return sanitized;
}

bool isValidIpv4Address(const std::string& ipAddress) {
    struct in_addr addr;
    return inet_pton(AF_INET, ipAddress.c_str(), &addr) == 1;
}

} // namespace

std::string normalizeMdnsModelValue(const std::string& model) {
    std::string normalized = trimQuotes(model);
    if (normalized.empty()) {
        elog_w(kTag, "mDNS model is empty, fallback to T32");
        return "T32";
    }

    std::string upperModel;
    upperModel.reserve(normalized.size());
    for (char c : normalized) {
        upperModel.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }

    if (upperModel == "CXXX") {
        elog_w(kTag, "mDNS model %s is invalid, fallback to T32", normalized.c_str());
        return "T32";
    }

    return normalized;
}

std::shared_ptr<MdnsService> MdnsService::getInstance() {
    static std::shared_ptr<MdnsService> instance(new MdnsService());
    return instance;
}

bool MdnsService::start(const MdnsServiceParams& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
    return startLocked(params);
}

bool MdnsService::refresh(const MdnsServiceParams& params) {
    return start(params);
}

bool MdnsService::updateStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) {
        params_.txt.status = status;
        return true;
    }

    MdnsServiceParams updated = params_;
    updated.txt.status = status;
    stopLocked();
    return startLocked(updated);
}

void MdnsService::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    stopLocked();
}

bool MdnsService::isRunning() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_;
}

bool MdnsService::startLocked(const MdnsServiceParams& params) {
    params_ = normalizeParams(params);
    if (!isValidIpv4Address(params_.ipAddress)) {
        elog_e(kTag, "Invalid IPv4 address: %s", params_.ipAddress.c_str());
        return false;
    }

    server_ = mdnsd_start();
    if (server_ == nullptr) {
        elog_e(kTag, "mdnsd_start failed");
        return false;
    }

    mdnsd_set_hostname(server_, params_.hostName.c_str(), inet_addr(params_.ipAddress.c_str()));
    if (!registerServiceLocked()) {
        elog_e(kTag, "Failed to register mDNS service");
        mdnsd_stop(server_);
        server_ = nullptr;
        return false;
    }

    running_ = true;
    elog_i(kTag, "Started mDNS service %s on %s (%s)",
           params_.serviceType.c_str(),
           params_.interfaceName.c_str(),
           params_.ipAddress.c_str());
    return true;
}

void MdnsService::stopLocked() {
    if (server_ != nullptr) {
        mdnsd_stop(server_);
        server_ = nullptr;
    }

    running_ = false;
}

MdnsServiceParams MdnsService::normalizeParams(const MdnsServiceParams& params) const {
    MdnsServiceParams normalized = params;
    normalized.serviceType = normalizeServiceType(params.serviceType);
    normalized.instanceName = normalizeInstanceName(params.instanceName);
    normalized.hostName = normalizeHostName(params.hostName);
    normalized.ipAddress = trimQuotes(params.ipAddress);
    normalized.interfaceName = trimQuotes(params.interfaceName);
    normalized.txt.deviceFamily = trimQuotes(params.txt.deviceFamily);
    if (normalized.txt.deviceFamily.empty()) {
        normalized.txt.deviceFamily = kDefaultMdnsDeviceFamily;
    }
    normalized.txt.model = normalizeMdnsModelValue(params.txt.model);
    normalized.txt.serialNumber = trimQuotes(params.txt.serialNumber);
    normalized.txt.firmwareVersion = trimQuotes(params.txt.firmwareVersion);
    normalized.txt.macAddress = trimQuotes(params.txt.macAddress);
    normalized.txt.status = trimQuotes(params.txt.status);
    if (normalized.txt.status.empty()) {
        normalized.txt.status = "ready";
    }
    return normalized;
}

bool MdnsService::registerServiceLocked() {
    std::vector<std::string> txtRecords = MdnsTxtRecord::build(params_.txt);
    std::vector<const char*> txtPtrs;
    txtPtrs.reserve(txtRecords.size() + 1);
    for (const auto& record : txtRecords) {
        txtPtrs.push_back(record.c_str());
    }
    txtPtrs.push_back(nullptr);

    struct mdns_service* service = mdnsd_register_svc(
        server_,
        params_.instanceName.c_str(),
        params_.serviceType.c_str(),
        params_.txt.ctrlPort,
        nullptr,
        txtPtrs.empty() ? nullptr : txtPtrs.data());

    if (service == nullptr) {
        return false;
    }

    mdns_service_destroy(service);
    return true;
}

} // namespace service
