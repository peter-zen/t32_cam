#include "CameraPropertyService.h"

#include "../../common/Common.h"
#include "../../config/devconf/DeviceConfig.h"
#include "../../config/env/EnvManager.h"
#include "../../config/setting/Settings.h"

#include <elog.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

#define TAG "PROP"

namespace service {

namespace {

struct VideoMode {
    int index = 0;
    int width = 0;
    int height = 0;
    int fps = 0;
};

const char* propertyTypeToString(CameraPropertyType type) {
    switch (type) {
    case CameraPropertyType::ENUM:
        return "enum";
    case CameraPropertyType::INTEGER:
        return "int";
    case CameraPropertyType::BOOLEAN:
        return "bool";
    default:
        return "string";
    }
}

std::vector<std::string> buildPropertyNames() {
    return {
        "resolution",
        "fps",
        "bitrate",
        "pir_enabled",
        "pir_sensitivity",
        "loop_recording",
        "max_record_duration",
        "timestamp_overlay",
    };
}

std::string resolutionToString(int width, int height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

bool parseIntString(const std::string& input, int& value) {
    if (input.empty()) {
        return false;
    }

    char* end = nullptr;
    long parsed = std::strtol(input.c_str(), &end, 10);
    if (end == nullptr || *end != '\0') {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

bool parseBoolValue(const Json::Value& value, bool& parsed) {
    if (value.isBool()) {
        parsed = value.asBool();
        return true;
    }

    if (value.isInt()) {
        int intValue = value.asInt();
        if (intValue == 0 || intValue == 1) {
            parsed = intValue == 1;
            return true;
        }
        return false;
    }

    if (value.isString()) {
        std::string stringValue = value.asString();
        std::transform(stringValue.begin(),
                       stringValue.end(),
                       stringValue.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (stringValue == "true" || stringValue == "1") {
            parsed = true;
            return true;
        }
        if (stringValue == "false" || stringValue == "0") {
            parsed = false;
            return true;
        }
    }

    return false;
}

bool parseIntValue(const Json::Value& value, int& parsed) {
    if (value.isInt()) {
        parsed = value.asInt();
        return true;
    }

    if (value.isUInt()) {
        parsed = static_cast<int>(value.asUInt());
        return true;
    }

    if (value.isString()) {
        return parseIntString(value.asString(), parsed);
    }

    return false;
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

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, value);
}

void appendVideoMode(std::vector<VideoMode>& modes, int width, int height, int fps) {
    VideoMode mode;
    mode.index = static_cast<int>(modes.size());
    mode.width = width;
    mode.height = height;
    mode.fps = fps;
    modes.push_back(mode);
}

std::vector<VideoMode> buildSupportedVideoModes() {
    std::vector<VideoMode> modes;
    int vidMaxSize = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_MVIDEO, 8);

    auto appendHdAndFhd = [&modes]() {
        appendVideoMode(modes, 1280, 720, 30);
        appendVideoMode(modes, 1280, 720, 60);
#ifdef VIDEO_SIZE_HD_120FPS
        appendVideoMode(modes, 1280, 720, 120);
#endif
#ifdef VIDEO_SIZE_HD_240FPS
        appendVideoMode(modes, 1280, 720, 240);
#endif
        appendVideoMode(modes, 1920, 1080, 30);
        appendVideoMode(modes, 1920, 1080, 60);
#ifdef VIDEO_SIZE_FHD_120FPS
        appendVideoMode(modes, 1920, 1080, 120);
#endif
    };

    if (vidMaxSize == 8 || vidMaxSize == 4) {
        appendHdAndFhd();
        appendVideoMode(modes, 2560, 1440, 30);
        appendVideoMode(modes, 3840, 2160, 30);
    } else if (vidMaxSize == 2) {
        appendHdAndFhd();
        appendVideoMode(modes, 2560, 1440, 30);
    } else if (vidMaxSize == 1) {
        appendHdAndFhd();
    } else {
        appendHdAndFhd();
        appendVideoMode(modes, 2560, 1440, 30);
        appendVideoMode(modes, 3840, 2160, 30);
    }

    if (modes.empty()) {
        appendVideoMode(modes, 1920, 1080, 30);
    }

    return modes;
}

const VideoMode& getDefaultVideoMode(const std::vector<VideoMode>& modes) {
    for (const auto& mode : modes) {
        if (mode.width == 1920 && mode.height == 1080 && mode.fps == 30) {
            return mode;
        }
    }

    for (const auto& mode : modes) {
        if (mode.width == 1920 && mode.height == 1080) {
            return mode;
        }
    }

    return modes.front();
}

const VideoMode& getCurrentVideoMode(const std::vector<VideoMode>& modes) {
    Settings* settings = Settings::getInstance().get();
    int videoSize = static_cast<int>(settings->videoSize);
    if (videoSize < 0 || videoSize >= static_cast<int>(modes.size())) {
        return getDefaultVideoMode(modes);
    }
    return modes[videoSize];
}

const VideoMode* findModeByResolutionFps(const std::vector<VideoMode>& modes, int width, int height, int fps) {
    for (const auto& mode : modes) {
        if (mode.width == width && mode.height == height && mode.fps == fps) {
            return &mode;
        }
    }
    return nullptr;
}

const VideoMode* findFirstModeByResolution(const std::vector<VideoMode>& modes, int width, int height) {
    for (const auto& mode : modes) {
        if (mode.width == width && mode.height == height) {
            return &mode;
        }
    }
    return nullptr;
}

int resolutionBucketDefaultMbps(int width, int height) {
    if (width >= 3840 || height >= 2160) {
        return 32;
    }
    if (width >= 1920 || height >= 1080) {
        return 16;
    }
    return 8;
}

int getConfiguredBucketMbps(const Settings* settings, int width, int height) {
    DeviceConfig* deviceConfig = DeviceConfig::getInstance().get();
    if (width >= 3840 || height >= 2160) {
        int settingsValue = static_cast<int>(settings->bitRate_4k);
        if (settingsValue > 0) {
            return settingsValue;
        }
        return deviceConfig->get(INI_SECTION_SYS, INI_KEY_BITRATE_4K, 32);
    }

    if (width >= 1920 || height >= 1080) {
        int settingsValue = static_cast<int>(settings->bitRate_1080p);
        if (settingsValue > 0) {
            return settingsValue;
        }
        return deviceConfig->get(INI_SECTION_SYS, INI_KEY_BITRATE_1080P, 16);
    }

    int settingsValue = static_cast<int>(settings->bitRate_720p);
    if (settingsValue > 0) {
        return settingsValue;
    }
    return deviceConfig->get(INI_SECTION_SYS, INI_KEY_BITRATE_720P, 8);
}

int currentBitrateKbpsForMode(const VideoMode& mode) {
    Settings* settings = Settings::getInstance().get();
    int bucketMbps = getConfiguredBucketMbps(settings, mode.width, mode.height);
    return bucketMbps * 1024;
}

void writeBitrateForMode(const VideoMode& mode, int bitrateKbps) {
    int bucketMbps = std::max(1, (bitrateKbps + 512) / 1024);
    Settings* settings = Settings::getInstance().get();
    auto deviceConfig = DeviceConfig::getInstance();

    if (mode.width >= 3840 || mode.height >= 2160) {
        settings->bitRate_4k = static_cast<uint8_t>(bucketMbps);
        deviceConfig->set(INI_SECTION_SYS, INI_KEY_BITRATE_4K, bucketMbps);
        return;
    }

    if (mode.width >= 1920 || mode.height >= 1080) {
        settings->bitRate_1080p = static_cast<uint8_t>(bucketMbps);
        deviceConfig->set(INI_SECTION_SYS, INI_KEY_BITRATE_1080P, bucketMbps);
        return;
    }

    settings->bitRate_720p = static_cast<uint8_t>(bucketMbps);
    deviceConfig->set(INI_SECTION_SYS, INI_KEY_BITRATE_720P, bucketMbps);
}

int currentVideoLengthSeconds() {
    Settings* settings = Settings::getInstance().get();
    int seconds = (static_cast<int>(settings->videoLength_h) << 8) + static_cast<int>(settings->videoLength_l);
    return seconds > 0 ? seconds : 30;
}

void writeVideoLengthSeconds(int seconds) {
    Settings* settings = Settings::getInstance().get();
    settings->videoLength_h = static_cast<uint8_t>((seconds >> 8) & 0xFF);
    settings->videoLength_l = static_cast<uint8_t>(seconds & 0xFF);
}

} // namespace

CameraPropertyService& CameraPropertyService::getInstance() {
    static CameraPropertyService instance;
    return instance;
}

std::vector<std::string> CameraPropertyService::getPropertyNames() const {
    return buildPropertyNames();
}

bool CameraPropertyService::hasProperty(const std::string& name) const {
    const auto propertyNames = buildPropertyNames();
    return std::find(propertyNames.begin(), propertyNames.end(), name) != propertyNames.end();
}

CameraPropertySchema CameraPropertyService::buildSchema(const std::string& name) const {
    CameraPropertySchema schema;
    schema.name = name;

    const std::vector<VideoMode> modes = buildSupportedVideoModes();
    const VideoMode& currentMode = getCurrentVideoMode(modes);
    const VideoMode& defaultMode = getDefaultVideoMode(modes);

    if (name == "resolution") {
        schema.displayName = "分辨率";
        schema.type = CameraPropertyType::ENUM;
        std::set<std::string> uniqueOptions;
        for (const auto& mode : modes) {
            uniqueOptions.insert(resolutionToString(mode.width, mode.height));
        }
        for (const auto& option : uniqueOptions) {
            schema.options.push_back(Json::Value(option));
        }
        schema.defaultValue = resolutionToString(defaultMode.width, defaultMode.height);
        return schema;
    }

    if (name == "fps") {
        schema.displayName = "帧率";
        schema.type = CameraPropertyType::INTEGER;
        std::set<int> uniqueOptions;
        for (const auto& mode : modes) {
            if (mode.width == currentMode.width && mode.height == currentMode.height) {
                uniqueOptions.insert(mode.fps);
            }
        }
        for (int option : uniqueOptions) {
            schema.options.push_back(Json::Value(option));
        }
        schema.unit = "fps";
        schema.defaultValue = defaultMode.fps;
        return schema;
    }

    if (name == "bitrate") {
        schema.displayName = "码率";
        schema.type = CameraPropertyType::INTEGER;
        schema.range.enabled = true;
        schema.range.min = 1024;
        schema.range.max = 32768;
        schema.range.step = 1024;
        schema.unit = "kbps";
        schema.defaultValue = resolutionBucketDefaultMbps(defaultMode.width, defaultMode.height) * 1024;
        return schema;
    }

    if (name == "pir_enabled") {
        schema.displayName = "PIR开关";
        schema.type = CameraPropertyType::BOOLEAN;
        schema.defaultValue = true;
        return schema;
    }

    if (name == "pir_sensitivity") {
        schema.displayName = "PIR灵敏度";
        schema.type = CameraPropertyType::INTEGER;
        schema.options.push_back(Json::Value(1));
        schema.options.push_back(Json::Value(2));
        schema.options.push_back(Json::Value(3));
        schema.defaultValue = 2;
        return schema;
    }

    if (name == "loop_recording") {
        schema.displayName = "循环录像";
        schema.type = CameraPropertyType::BOOLEAN;
        schema.defaultValue = true;
        return schema;
    }

    if (name == "max_record_duration") {
        schema.displayName = "最大录像时长";
        schema.type = CameraPropertyType::INTEGER;
        schema.range.enabled = true;
        schema.range.min = 10;
        schema.range.max = 3600;
        schema.range.step = 10;
        schema.unit = "s";
        schema.defaultValue = 30;
        return schema;
    }

    if (name == "timestamp_overlay") {
        schema.displayName = "时间戳叠加";
        schema.type = CameraPropertyType::BOOLEAN;
        schema.defaultValue = true;
        return schema;
    }

    return schema;
}

Json::Value CameraPropertyService::getDefaultValue(const std::string& name) const {
    return buildSchema(name).defaultValue;
}

Json::Value CameraPropertyService::readValue(const std::string& name) const {
    Settings* settings = Settings::getInstance().get();
    const std::vector<VideoMode> modes = buildSupportedVideoModes();
    const VideoMode& currentMode = getCurrentVideoMode(modes);

    if (name == "resolution") {
        return resolutionToString(currentMode.width, currentMode.height);
    }

    if (name == "fps") {
        return currentMode.fps;
    }

    if (name == "bitrate") {
        return currentBitrateKbpsForMode(currentMode);
    }

    if (name == "pir_enabled") {
        return settings->pirEn != 0;
    }

    if (name == "pir_sensitivity") {
        int sensitivity = static_cast<int>(settings->ckPirSensitivity);
        if (sensitivity < 1 || sensitivity > 3) {
            sensitivity = 2;
        }
        return sensitivity;
    }

    if (name == "loop_recording") {
        return settings->autoCover != 0;
    }

    if (name == "max_record_duration") {
        return currentVideoLengthSeconds();
    }

    if (name == "timestamp_overlay") {
        return settings->stampEn != 0;
    }

    return Json::nullValue;
}

Json::Value CameraPropertyService::buildPropertyJson(const std::string& name, bool includeName) const {
    const CameraPropertySchema schema = buildSchema(name);
    Json::Value property(Json::objectValue);
    if (includeName) {
        property["name"] = name;
    }

    property["value"] = readValue(name);
    property["display_name"] = schema.displayName;
    property["type"] = propertyTypeToString(schema.type);
    property["readonly"] = schema.readonly;
    property["persistent"] = schema.persistent;
    property["default_value"] = schema.defaultValue;

    if (!schema.options.empty()) {
        Json::Value options(Json::arrayValue);
        for (const auto& option : schema.options) {
            options.append(option);
        }
        property["options"] = options;
    }

    if (schema.range.enabled) {
        property["min"] = schema.range.min;
        property["max"] = schema.range.max;
        property["step"] = schema.range.step;
    }

    if (!schema.unit.empty()) {
        property["unit"] = schema.unit;
    }

    return property;
}

Json::Value CameraPropertyService::getAllPropertiesJson() const {
    Json::Value root(Json::objectValue);
    for (const auto& name : buildPropertyNames()) {
        root[name] = buildPropertyJson(name, false);
    }
    return root;
}

bool CameraPropertyService::getPropertyJson(const std::string& name,
                                            Json::Value& outProperty,
                                            std::string* error) const {
    if (!hasProperty(name)) {
        if (error) {
            *error = "Property not found";
        }
        return false;
    }

    outProperty = buildPropertyJson(name, true);
    return true;
}

int CameraPropertyService::getPropertyValueString(const std::string& name,
                                                  std::string& value,
                                                  std::string* error) const {
    if (!hasProperty(name)) {
        if (error) {
            *error = "Property not found";
        }
        return -1;
    }

    value = jsonValueToString(readValue(name));
    return 0;
}

bool CameraPropertyService::validateValue(const CameraPropertySchema& schema,
                                          const Json::Value& value,
                                          Json::Value& normalized,
                                          std::string& error) const {
    if (schema.readonly) {
        error = "Property is readonly";
        return false;
    }

    if (schema.type == CameraPropertyType::BOOLEAN) {
        bool parsed = false;
        if (!parseBoolValue(value, parsed)) {
            error = "Invalid boolean value";
            return false;
        }
        normalized = parsed;
        return true;
    }

    if (schema.type == CameraPropertyType::INTEGER) {
        int parsed = 0;
        if (!parseIntValue(value, parsed)) {
            error = "Invalid integer value";
            return false;
        }

        if (!schema.options.empty()) {
            for (const auto& option : schema.options) {
                if (option.isInt() && option.asInt() == parsed) {
                    normalized = parsed;
                    return true;
                }
            }
            error = "Invalid value";
            return false;
        }

        if (schema.range.enabled) {
            if (parsed < schema.range.min || parsed > schema.range.max) {
                error = "Value out of range";
                return false;
            }
            if (((parsed - schema.range.min) % schema.range.step) != 0) {
                error = "Value does not match step";
                return false;
            }
        }

        normalized = parsed;
        return true;
    }

    std::string parsed;
    if (value.isString()) {
        parsed = value.asString();
    } else {
        parsed = jsonValueToString(value);
    }

    if (!schema.options.empty()) {
        for (const auto& option : schema.options) {
            if (option.isString() && option.asString() == parsed) {
                normalized = parsed;
                return true;
            }
        }
        error = "Invalid value";
        return false;
    }

    normalized = parsed;
    return true;
}

int CameraPropertyService::writeValue(const std::string& name, const Json::Value& normalized, std::string* error) {
    Settings* settings = Settings::getInstance().get();
    const std::vector<VideoMode> modes = buildSupportedVideoModes();
    const VideoMode& currentMode = getCurrentVideoMode(modes);

    if (name == "resolution") {
        std::string resolution = normalized.asString();
        int width = 0;
        int height = 0;
        const size_t pos = resolution.find('x');
        if (pos == std::string::npos ||
            !parseIntString(resolution.substr(0, pos), width) ||
            !parseIntString(resolution.substr(pos + 1), height)) {
            if (error) {
                *error = "Invalid resolution format";
            }
            return -1;
        }

        const VideoMode* targetMode = findModeByResolutionFps(modes, width, height, currentMode.fps);
        if (!targetMode) {
            targetMode = findFirstModeByResolution(modes, width, height);
        }
        if (!targetMode) {
            if (error) {
                *error = "Unsupported resolution";
            }
            return -1;
        }

        settings->videoSize = static_cast<uint8_t>(targetMode->index);
    } else if (name == "fps") {
        int fps = normalized.asInt();
        const VideoMode* targetMode = findModeByResolutionFps(modes, currentMode.width, currentMode.height, fps);
        if (!targetMode) {
            if (error) {
                *error = "Unsupported fps for current resolution";
            }
            return -1;
        }

        settings->videoSize = static_cast<uint8_t>(targetMode->index);
    } else if (name == "bitrate") {
        writeBitrateForMode(currentMode, normalized.asInt());
    } else if (name == "pir_enabled") {
        settings->pirEn = normalized.asBool() ? 1 : 0;
    } else if (name == "pir_sensitivity") {
        settings->ckPirSensitivity = static_cast<uint8_t>(normalized.asInt());
    } else if (name == "loop_recording") {
        settings->autoCover = normalized.asBool() ? 1 : 0;
    } else if (name == "max_record_duration") {
        writeVideoLengthSeconds(normalized.asInt());
    } else if (name == "timestamp_overlay") {
        settings->stampEn = normalized.asBool() ? 1 : 0;
    } else {
        if (error) {
            *error = "Property not found";
        }
        return -1;
    }

    if (!persistSettings()) {
        if (error) {
            *error = "Failed to persist settings";
        }
        return -1;
    }

    if (!persistDeviceConfig()) {
        if (error) {
            *error = "Failed to persist device config";
        }
        return -1;
    }

    return 0;
}

bool CameraPropertyService::persistSettings() const {
    std::string settingFilePath = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", "");
    if (settingFilePath.empty()) {
        return true;
    }

    bool ok = Settings::getInstance()->saveToJsonFile(settingFilePath);
    if (!ok) {
        elog_e(TAG, "Failed to save settings: %s", settingFilePath.c_str());
    }
    return ok;
}

bool CameraPropertyService::persistDeviceConfig() const {
    std::string configFilePath = EnvManager::getInstance()->getEnv("CONFIG_FILE", "");
    if (configFilePath.empty()) {
        return true;
    }

    bool ok = DeviceConfig::getInstance()->flush();
    if (!ok) {
        elog_w(TAG, "DeviceConfig flush skipped or failed");
    }
    return ok;
}

int CameraPropertyService::setPropertyValue(const std::string& name,
                                            const Json::Value& value,
                                            Json::Value* outProperty,
                                            std::string* error) {
    if (!hasProperty(name)) {
        if (error) {
            *error = "Property not found";
        }
        return -1;
    }

    const CameraPropertySchema schema = buildSchema(name);
    Json::Value normalized;
    std::string validateError;
    if (!validateValue(schema, value, normalized, validateError)) {
        if (error) {
            *error = validateError;
        }
        return -1;
    }

    int ret = writeValue(name, normalized, error);
    if (ret == 0 && outProperty) {
        *outProperty = buildPropertyJson(name, true);
    }
    return ret;
}

int CameraPropertyService::resetProperties(const std::vector<std::string>& names,
                                           Json::Value& appliedProperties,
                                           std::string* error) {
    appliedProperties = Json::Value(Json::objectValue);

    std::set<std::string> targets;
    if (names.empty()) {
        const auto allNames = buildPropertyNames();
        targets.insert(allNames.begin(), allNames.end());
    } else {
        targets.insert(names.begin(), names.end());
    }

    int resetCount = 0;
    for (const auto& name : buildPropertyNames()) {
        if (targets.find(name) == targets.end()) {
            continue;
        }

        Json::Value propertyJson;
        std::string localError;
        if (setPropertyValue(name, getDefaultValue(name), &propertyJson, &localError) != 0) {
            if (error) {
                *error = localError;
            }
            return -1;
        }

        appliedProperties[name] = propertyJson["value"];
        resetCount++;
    }

    return resetCount;
}

void CameraPropertyService::getVideoRecordConfig(int& width, int& height, int& fps, int& bitrateKbps) const {
    const std::vector<VideoMode> modes = buildSupportedVideoModes();
    const VideoMode& currentMode = getCurrentVideoMode(modes);
    width = currentMode.width;
    height = currentMode.height;
    fps = currentMode.fps;
    bitrateKbps = currentBitrateKbpsForMode(currentMode);
}

} // namespace service
