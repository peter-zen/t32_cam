#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace service {

enum class ParameterClassification {
    FACTORY,
    PROPERTY,
    STATUS,
    COMMAND,
};

enum class ParameterValueType {
    NUMBER,
    STRING,
    BOOLEAN,
    STRING_ARRAY,
    COMMAND,
};

enum class ParameterPermission {
    FACTORY,
    READ,
    WRITE,
    READ_WRITE,
    COMMAND,
};

enum class ParameterAvailability {
    REAL,
    PERSISTED,
    COMPUTED,
    PLACEHOLDER,
};

enum class ParameterStorageKind {
    NONE,
    SETTINGS,
    DEVICE_CONFIG,
    PRODUCT,
    COMPUTED,
    PLACEHOLDER,
    COMMAND,
    MCU,
};

struct ParameterRange {
    bool enabled = false;
    int min = 0;
    int max = 0;
    int step = 1;
};

struct ParameterDependency {
    std::string switchName;
    int expectedValue = 1;
    std::string disabledReason;
};

struct ParameterStorageBinding {
    ParameterStorageKind kind = ParameterStorageKind::NONE;
    std::string section;
    std::string key;
    std::string member;
};

struct ParameterDefinition {
    std::string id;
    std::string rawName;
    std::string displayName;
    std::vector<std::string> legacyAliases;
    ParameterClassification classification = ParameterClassification::PROPERTY;
    std::string group;
    std::string chapter;
    ParameterValueType type = ParameterValueType::STRING;
    ParameterPermission permission = ParameterPermission::READ_WRITE;
    Json::Value defaultValue;
    std::vector<Json::Value> options;
    ParameterRange range;
    std::string unit;
    ParameterDependency dependency;
    ParameterStorageBinding storage;
    ParameterAvailability availability = ParameterAvailability::PLACEHOLDER;
};

const std::vector<ParameterDefinition>& getCameraParameterDefinitions();
std::vector<const ParameterDefinition*> getParameterDefinitions(ParameterClassification classification,
                                                               const std::string& group = "all");
const ParameterDefinition* findParameterDefinition(const std::string& name,
                                                   ParameterClassification classification,
                                                   bool includeAliases = true);
const ParameterDefinition* findAnyParameterDefinition(const std::string& name, bool includeAliases = true);

const char* parameterClassificationToString(ParameterClassification classification);
const char* parameterValueTypeToString(ParameterValueType type);
const char* parameterPermissionToString(ParameterPermission permission);
const char* parameterAvailabilityToString(ParameterAvailability availability);
const char* parameterStorageKindToString(ParameterStorageKind kind);

// Binding factory for a STATUS / PROPERTY field whose live value comes
// from McuService. The `member` string is the McuService getter name
// (e.g. "battery1Voltage", "cds", "mcuVersion") and is resolved by
// readMcuValue() in CameraStatusService / CameraPropertyService.
ParameterStorageBinding mcuBinding(const std::string& member);

// Resolve a `member` set by mcuBinding() into a Json::Value. Returns an
// empty Json::Value if the name is unknown. Defined in the .cpp so callers
// don't need to pull in McuService.h.
Json::Value readMcuValue(const std::string& member);

} // namespace service
