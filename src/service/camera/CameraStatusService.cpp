#include "CameraStatusService.h"

#include "CameraParameterRegistry.h"

#include "../../config/devconf/DeviceConfig.h"
#include "../../config/setting/Settings.h"

#include <algorithm>
#include <cctype>

namespace service {

namespace {

std::string toLowerCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(),
                   lowered.end(),
                   lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

bool groupMatches(const std::string& actual, const std::string& requested) {
    return requested.empty() || requested == "all" || toLowerCopy(actual) == toLowerCopy(requested);
}

std::string jsonValueToString(const Json::Value& value) {
    if (value.isString()) {
        return value.asString();
    }
    if (value.isBool()) {
        return value.asBool() ? "true" : "false";
    }
    if (value.isInt()) {
        return std::to_string(value.asInt());
    }
    if (value.isUInt()) {
        return std::to_string(value.asUInt());
    }
    return "";
}

Json::Value readStatusValue(const ParameterDefinition& definition) {
    if (definition.storage.kind == ParameterStorageKind::DEVICE_CONFIG) {
        DeviceConfig* deviceConfig = DeviceConfig::getInstance().get();
        if (definition.type == ParameterValueType::NUMBER || definition.type == ParameterValueType::BOOLEAN) {
            return deviceConfig->get(definition.storage.section,
                                     definition.storage.key,
                                     definition.defaultValue.isInt() ? definition.defaultValue.asInt() : 0);
        }
        return deviceConfig->get(definition.storage.section,
                                 definition.storage.key,
                                 definition.defaultValue.isString() ? definition.defaultValue.asString()
                                                                    : jsonValueToString(definition.defaultValue));
    }

    if (definition.storage.kind == ParameterStorageKind::SETTINGS) {
        Settings* settings = Settings::getInstance().get();
        if (definition.storage.member == "duid") {
            return Json::Value(settings->duid);
        }
        if (definition.storage.member == "devName") {
            return Json::Value(std::string(settings->devName));
        }
    }

    if (definition.storage.kind == ParameterStorageKind::COMPUTED &&
        definition.storage.member == "fw_version") {
#ifdef CAMERA_VERSION
        return Json::Value(CAMERA_VERSION);
#else
        return definition.defaultValue;
#endif
    }

    return definition.defaultValue;
}

Json::Value buildStatusEntry(const ParameterDefinition& definition) {
    Json::Value entry(Json::objectValue);
    entry["id"] = definition.id;
    entry["name"] = definition.rawName;
    entry["raw_name"] = definition.rawName;
    entry["group"] = definition.group;
    entry["chapter"] = definition.chapter;
    entry["type"] = parameterValueTypeToString(definition.type);
    entry["value"] = readStatusValue(definition);
    entry["available"] = definition.availability != ParameterAvailability::PLACEHOLDER;
    entry["source"] = parameterAvailabilityToString(definition.availability);
    if (definition.availability == ParameterAvailability::PLACEHOLDER) {
        entry["reason"] = "not_implemented";
    }
    return entry;
}

} // namespace

CameraStatusService& CameraStatusService::getInstance() {
    static CameraStatusService instance;
    return instance;
}

Json::Value CameraStatusService::getStatusJson(const std::string& group) const {
    const std::string requestedGroup = group.empty() ? "all" : group;
    Json::Value root(Json::objectValue);
    Json::Value groups(Json::objectValue);
    int count = 0;

    for (const auto& definition : getCameraParameterDefinitions()) {
        if (definition.classification != ParameterClassification::STATUS ||
            !groupMatches(definition.group, requestedGroup)) {
            continue;
        }

        groups[toLowerCopy(definition.group)].append(buildStatusEntry(definition));
        count++;
    }

    root["group"] = requestedGroup;
    root["count"] = count;
    root["status"] = groups;
    return root;
}

} // namespace service
