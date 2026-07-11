#include "CameraPropertyService.h"

#include "../../common/Common.h"
#include "../../config/devconf/DeviceConfig.h"
#include "../../config/devconf/ProductConfig.h"
#include "../../config/env/EnvManager.h"
#include "../../config/setting/Settings.h"

#include <elog.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

std::string toLowerCopy(const std::string& value) {
    std::string lowered = value;
    std::transform(lowered.begin(),
                   lowered.end(),
                   lowered.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lowered;
}

bool equalsIgnoreCase(const std::string& lhs, const std::string& rhs) {
    return toLowerCopy(lhs) == toLowerCopy(rhs);
}

bool includeTokenEnabled(const std::string& include, const std::string& token) {
    if (include.empty()) {
        return true;
    }

    std::stringstream ss(include);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item.erase(std::remove_if(item.begin(),
                                  item.end(),
                                  [](unsigned char ch) { return std::isspace(ch) != 0; }),
                   item.end());
        if (item == "all" || item == token) {
            return true;
        }
    }
    return false;
}

const std::vector<std::string>& specPropertyGroupOrder() {
    static const std::vector<std::string> order = {
        "Camera_Setting",
        "Audio_Setting",
        "PIR_Setting",
        "Timer_Setting",
        "Network_Setting",
        "Server_Setting",
        "System_Setting",
        "AI_Setting",
    };
    return order;
}

std::string canonicalPropertyGroup(const std::string& requestedGroup) {
    for (const auto& group : specPropertyGroupOrder()) {
        if (equalsIgnoreCase(group, requestedGroup)) {
            return group;
        }
    }
    return requestedGroup;
}

bool isWritableRegistryStorage(const ParameterDefinition& definition) {
    return definition.storage.kind == ParameterStorageKind::SETTINGS ||
           definition.storage.kind == ParameterStorageKind::DEVICE_CONFIG;
}

template<typename T>
std::string numberToString(T value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string resolutionToString(int width, int height) {
    return numberToString(width) + "x" + numberToString(height);
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
        return numberToString(value.asInt());
    }
    if (value.isUInt()) {
        return numberToString(value.asUInt());
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, value);
}

Json::Value settingByte(uint8_t value) {
    return Json::Value(static_cast<int>(value));
}

std::string formatTime(uint8_t hour, uint8_t minute) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u:%02u", static_cast<unsigned>(hour), static_cast<unsigned>(minute));
    return std::string(buf);
}

bool parseTimeValue(const Json::Value& value, uint8_t& hour, uint8_t& minute) {
    if (!value.isString()) {
        return false;
    }

    const std::string text = value.asString();
    const size_t pos = text.find(':');
    if (pos == std::string::npos) {
        return false;
    }

    int parsedHour = 0;
    int parsedMinute = 0;
    if (!parseIntString(text.substr(0, pos), parsedHour) ||
        !parseIntString(text.substr(pos + 1), parsedMinute)) {
        return false;
    }
    if (parsedHour < 0 || parsedHour > 23 || parsedMinute < 0 || parsedMinute > 59) {
        return false;
    }

    hour = static_cast<uint8_t>(parsedHour);
    minute = static_cast<uint8_t>(parsedMinute);
    return true;
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
    appendVideoMode(modes, 2560, 1440, 30);
    appendVideoMode(modes, 3840, 2160, 30);

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
    // 2026-06-10: mapping table, 见 doc/knowledge/bugs/T32-crc-fix-verification-2026-06-10.md
    if (width >= 3840 || height >= 2160) {
        return 8;   // 4K
    }
    if (width >= 2560 || height >= 1440) {
        return 6;   // 2.5K
    }
    if (width >= 1920 || height >= 1080) {
        return 4;   // 1080P
    }
    return 2;       // 720P
}

int getConfiguredBucketMbps(const Settings* settings, int width, int height) {
    if (width >= 3840 || height >= 2160) {
        int settingsValue = static_cast<int>(settings->bitRate_4k);
        return settingsValue > 0 ? settingsValue : 8;
    }

    // 2026-06-10: 新增 2.5K (2560x1440) 桶, 6 Mbps
    if (width >= 2560 || height >= 1440) {
        int settingsValue = static_cast<int>(settings->bitRate_2_5k);
        return settingsValue > 0 ? settingsValue : 6;
    }

    if (width >= 1920 || height >= 1080) {
        int settingsValue = static_cast<int>(settings->bitRate_1080p);
        return settingsValue > 0 ? settingsValue : 4;
    }

    int settingsValue = static_cast<int>(settings->bitRate_720p);
    return settingsValue > 0 ? settingsValue : 2;
}

int currentBitrateKbpsForMode(const VideoMode& mode) {
    Settings* settings = Settings::getInstance().get();
    int bucketMbps = getConfiguredBucketMbps(settings, mode.width, mode.height);
    return bucketMbps * 1024;
}

void writeBitrateForMode(const VideoMode& mode, int bitrateKbps) {
    int bucketMbps = std::max(1, (bitrateKbps + 512) / 1024);
    Settings* settings = Settings::getInstance().get();

    if (mode.width >= 3840 || mode.height >= 2160) {
        settings->bitRate_4k = static_cast<uint8_t>(bucketMbps);
        return;
    }

    // 2026-06-10: 新增 2.5K 桶
    if (mode.width >= 2560 || mode.height >= 1440) {
        settings->bitRate_2_5k = static_cast<uint8_t>(bucketMbps);
        return;
    }

    if (mode.width >= 1920 || mode.height >= 1080) {
        settings->bitRate_1080p = static_cast<uint8_t>(bucketMbps);
        return;
    }

    settings->bitRate_720p = static_cast<uint8_t>(bucketMbps);
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

std::string videoModeToSpecString(const VideoMode& mode) {
    if (mode.width >= 3840 || mode.height >= 2160) {
        return "4K/" + numberToString(mode.fps) + "FPS";
    }
    if (mode.width >= 2560 || mode.height >= 1440) {
        return "2K/" + numberToString(mode.fps) + "FPS";
    }
    if (mode.width >= 1920 || mode.height >= 1080) {
        return "1080P/" + numberToString(mode.fps) + "FPS";
    }
    return "720P/" + numberToString(mode.fps) + "FPS";
}

bool parseSpecVideoMode(const std::string& value, int& width, int& height, int& fps) {
    const size_t slash = value.find('/');
    const size_t fpsPos = value.find("FPS", slash == std::string::npos ? 0 : slash);
    if (slash == std::string::npos || fpsPos == std::string::npos) {
        return false;
    }

    const std::string size = value.substr(0, slash);
    if (size == "4K") {
        width = 3840;
        height = 2160;
    } else if (size == "2K") {
        width = 2560;
        height = 1440;
    } else if (size == "1080P") {
        width = 1920;
        height = 1080;
    } else if (size == "720P") {
        width = 1280;
        height = 720;
    } else {
        return false;
    }

    return parseIntString(value.substr(slash + 1, fpsPos - slash - 1), fps);
}

Json::Value storageBindingToJson(const ParameterStorageBinding& storage) {
    Json::Value root(Json::objectValue);
    root["kind"] = parameterStorageKindToString(storage.kind);
    if (!storage.section.empty()) {
        root["section"] = storage.section;
    }
    if (!storage.key.empty()) {
        root["key"] = storage.key;
    }
    if (!storage.member.empty()) {
        root["member"] = storage.member;
    }
    return root;
}

Json::Value rangeToJson(const ParameterRange& range) {
    Json::Value root(Json::objectValue);
    root["min"] = range.min;
    root["max"] = range.max;
    root["step"] = range.step;
    return root;
}

} // namespace

CameraPropertyService& CameraPropertyService::getInstance() {
    static CameraPropertyService instance;
    return instance;
}

std::vector<std::string> CameraPropertyService::getPropertyNames() const {
    return buildPropertyNames();
}

bool CameraPropertyService::hasLegacyProperty(const std::string& name) const {
    const auto propertyNames = buildPropertyNames();
    return std::find(propertyNames.begin(), propertyNames.end(), name) != propertyNames.end();
}

bool CameraPropertyService::hasProperty(const std::string& name) const {
    return hasLegacyProperty(name) ||
           findParameterDefinition(name, ParameterClassification::PROPERTY, true) != nullptr;
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

Json::Value CameraPropertyService::readRegistryValue(const ParameterDefinition& definition) const {
    Settings* settings = Settings::getInstance().get();

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

    if (definition.storage.kind == ParameterStorageKind::PRODUCT) {
        ProductConfig* productConfig = ProductConfig::getInstance().get();
        if (definition.type == ParameterValueType::NUMBER || definition.type == ParameterValueType::BOOLEAN) {
            return productConfig->get(definition.storage.section,
                                      definition.storage.key,
                                      definition.defaultValue.isInt() ? definition.defaultValue.asInt() : 0);
        }
        return productConfig->get(definition.storage.section,
                                  definition.storage.key,
                                  definition.defaultValue.isString() ? definition.defaultValue.asString()
                                                                     : jsonValueToString(definition.defaultValue));
    }

    if (definition.storage.kind == ParameterStorageKind::COMPUTED) {
        if (definition.storage.member == "fw_version") {
#ifdef CAMERA_VERSION
            return Json::Value(CAMERA_VERSION);
#else
            return definition.defaultValue;
#endif
        }
        return definition.defaultValue;
    }

    if (definition.storage.kind == ParameterStorageKind::MCU) {
        Json::Value v = readMcuValue(definition.storage.member);
        if (!v.isNull()) {
            return v;
        }
    }

    if (definition.storage.kind != ParameterStorageKind::SETTINGS) {
        return definition.defaultValue;
    }

    const std::string& member = definition.storage.member;
    if (member == "cameraMode") return settingByte(settings->cameraMode);
    if (member == "stillSize") {
        static const char* sizeNames[] = {
            "2M", "4M", "8M", "16M", "24M", "32M", "42M"
        };
        int index = static_cast<int>(settings->stillSize);
        if (index >= 0 && index < SNAP_IMG_SIZE_MAX) {
            return Json::Value(sizeNames[index]);
        }
        return definition.defaultValue;
    }
    if (member == "stillQuality") return settingByte(settings->stillQuality);
    if (member == "shootingInterval") return (static_cast<int>(settings->shootingInterval) * 100);
    if (member == "burstNumber") return settingByte(settings->burstNumber);
    if (member == "videoSize") {
        const std::vector<VideoMode> modes = buildSupportedVideoModes();
        return Json::Value(videoModeToSpecString(getCurrentVideoMode(modes)));
    }
    if (member == "bitrate") {
        const std::vector<VideoMode> modes = buildSupportedVideoModes();
        return currentBitrateKbpsForMode(getCurrentVideoMode(modes));
    }
    if (member == "videoCodec") return settingByte(settings->videoCodec);
    if (member == "videoRcMode") return settingByte(settings->videoRcMode);
    if (member == "audioRecordVolume") return settingByte(settings->audioRecordVolume);
    if (member == "audioRecordGain") return settingByte(settings->audioRecordGain);
    if (member == "videoLength") return currentVideoLengthSeconds();
    if (member == "pirEn") return settingByte(settings->pirEn);
    if (member == "ckPirSensitivity") return settingByte(settings->ckPirSensitivity);
    if (member == "shootingLimits") return settingByte(settings->shootingLimits);
    if (member == "timerEn") return settingByte(settings->timerEn);
    if (member == "timerLapse") return Json::Value(formatTime(settings->timerLapse_m, settings->timerLapse_s));
    if (member == "timer1s") return Json::Value(formatTime(settings->timer1s_h, settings->timer1s_m));
    if (member == "timer1e") return Json::Value(formatTime(settings->timer1e_h, settings->timer1e_m));
    if (member == "timer2s") return Json::Value(formatTime(settings->timer2s_h, settings->timer2s_m));
    if (member == "timer2e") return Json::Value(formatTime(settings->timer2e_h, settings->timer2e_m));
    if (member == "timer3s") return Json::Value(formatTime(settings->timer3s_h, settings->timer3s_m));
    if (member == "timer3e") return Json::Value(formatTime(settings->timer3e_h, settings->timer3e_m));
    if (member == "weekRepeats") return Json::Value(numberToString(static_cast<int>(settings->weekRepeats)));
    if (member == "stampEn") return settingByte(settings->stampEn);
    if (member == "autoCover") return settingByte(settings->autoCover);
    if (member == "heartRate") {
        int value = (static_cast<int>(settings->heartRate_0) << 24) |
                    (static_cast<int>(settings->heartRate_1) << 16) |
                    (static_cast<int>(settings->heartRate_2) << 8) |
                    static_cast<int>(settings->heartRate_3);
        return value;
    }
    if (member == "remote_wakeup") return settingByte(settings->remote_wakeup);
    if (member == "devName") return Json::Value(std::string(settings->devName));
    if (member == "duid") return Json::Value(settings->duid);
    // T25 Phase-3: user-mutable fields migrated from DeviceConfig
    if (member == "timezone") return Json::Value(settings->timezone);
    if (member == "upid") return Json::Value(settings->upid);
    if (member == "upwd") return Json::Value(settings->upwd);
    if (member == "lowVoltage") return Json::Value(settings->lowVoltage);
    if (member == "endVoltage") return Json::Value(settings->endVoltage);

    return definition.defaultValue;
}

bool CameraPropertyService::isParameterEnabled(const ParameterDefinition& definition,
                                               std::string* disabledReason) const {
    if (definition.dependency.switchName.empty()) {
        return true;
    }

    const ParameterDefinition* dependency = findAnyParameterDefinition(definition.dependency.switchName, false);
    if (!dependency) {
        if (disabledReason) {
            *disabledReason = "Dependency switch not found: " + definition.dependency.switchName;
        }
        return false;
    }

    const Json::Value value = readRegistryValue(*dependency);
    int intValue = 0;
    if (!parseIntValue(value, intValue)) {
        if (disabledReason) {
            *disabledReason = "Dependency switch is not numeric: " + definition.dependency.switchName;
        }
        return false;
    }

    bool enabled = false;
    if (definition.dependency.switchName == "Timer_Range_MAX") {
        if (definition.rawName.find("Timer_4") != std::string::npos) {
            enabled = intValue >= 4;
        } else if (definition.rawName.find("Timer_5") != std::string::npos) {
            enabled = intValue >= 5;
        } else {
            enabled = true;
        }
    } else {
        enabled = intValue == definition.dependency.expectedValue;
    }

    if (!enabled && disabledReason) {
        *disabledReason = definition.dependency.disabledReason.empty()
                              ? ("Disabled by " + definition.dependency.switchName)
                              : definition.dependency.disabledReason;
    }
    return enabled;
}

Json::Value CameraPropertyService::buildRegistryPropertyJson(const ParameterDefinition& definition,
                                                             bool includeSchema,
                                                             bool includeValue) const {
    Json::Value property(Json::objectValue);
    property["id"] = definition.id;
    property["name"] = definition.rawName;
    property["raw_name"] = definition.rawName;
    property["group"] = definition.group;
    property["classification"] = parameterClassificationToString(definition.classification);
    property["chapter"] = definition.chapter;
    property["source"] = parameterAvailabilityToString(definition.availability);

    std::string disabledReason;
    const bool enabled = isParameterEnabled(definition, &disabledReason);
    property["enabled"] = enabled;
    if (!enabled) {
        property["disabled_reason"] = disabledReason;
    }

    if (includeSchema) {
        property["display_name"] = definition.displayName;
        property["type"] = parameterValueTypeToString(definition.type);
        property["permission"] = parameterPermissionToString(definition.permission);
        property["readonly"] = definition.permission == ParameterPermission::READ ||
                               definition.permission == ParameterPermission::FACTORY ||
                               definition.permission == ParameterPermission::COMMAND;
        property["default"] = definition.defaultValue;
        property["default_value"] = definition.defaultValue;
        property["storage_binding"] = storageBindingToJson(definition.storage);
        if (!definition.legacyAliases.empty()) {
            Json::Value aliases(Json::arrayValue);
            for (const auto& alias : definition.legacyAliases) {
                aliases.append(alias);
            }
            property["legacy_aliases"] = aliases;
        }
        if (!definition.options.empty()) {
            Json::Value options(Json::arrayValue);
            for (const auto& option : definition.options) {
                options.append(option);
            }
            property["options"] = options;
        }
        if (definition.range.enabled) {
            property["range"] = rangeToJson(definition.range);
            property["min"] = definition.range.min;
            property["max"] = definition.range.max;
            property["step"] = definition.range.step;
        }
        if (!definition.unit.empty()) {
            property["unit"] = definition.unit;
        }
        if (!definition.dependency.switchName.empty()) {
            Json::Value dependency(Json::objectValue);
            dependency["switch"] = definition.dependency.switchName;
            dependency["expected"] = definition.dependency.expectedValue;
            dependency["disabled_reason"] = definition.dependency.disabledReason;
            property["dependency"] = dependency;
        }
    }

    if (includeValue) {
        property["value"] = readRegistryValue(definition);
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

Json::Value CameraPropertyService::getPropertiesJson(const std::string& group,
                                                     const std::string& include) const {
    const bool includeSchema = includeTokenEnabled(include, "schema");
    const bool includeValue = includeTokenEnabled(include, "value");
    const std::string requestedGroup = group.empty() ? "all" : group;
    const bool includeAllGroups = requestedGroup == "all";

    Json::Value root(Json::objectValue);
    root["group"] = requestedGroup;
    root["include_schema"] = includeSchema;
    root["include_value"] = includeValue;

    Json::Value orderedProperties(Json::arrayValue);
    int count = 0;

    std::vector<std::string> selectedGroups;
    if (includeAllGroups) {
        selectedGroups = specPropertyGroupOrder();
    } else {
        selectedGroups.push_back(canonicalPropertyGroup(requestedGroup));
    }

    for (const auto& currentGroup : selectedGroups) {
        Json::Value properties(Json::arrayValue);
        for (const auto& definition : getCameraParameterDefinitions()) {
            if (definition.classification != ParameterClassification::PROPERTY ||
                !equalsIgnoreCase(definition.group, currentGroup)) {
                continue;
            }

            properties.append(buildRegistryPropertyJson(definition, includeSchema, includeValue));
            count++;
        }

        if (!properties.empty()) {
            Json::Value groupEntry(Json::objectValue);
            groupEntry["group"] = currentGroup;
            groupEntry["items"] = properties;
            orderedProperties.append(groupEntry);
        }
    }

    root["count"] = count;
    root["properties"] = orderedProperties;
    return root;
}

bool CameraPropertyService::getPropertyJson(const std::string& name,
                                            Json::Value& outProperty,
                                            std::string* error) const {
    if (hasLegacyProperty(name)) {
        outProperty = buildPropertyJson(name, true);
        return true;
    }

    if (!getRegistryPropertyJson(name, outProperty, "schema,value", error)) {
        return false;
    }

    return true;
}

bool CameraPropertyService::getRegistryPropertyJson(const std::string& name,
                                                    Json::Value& outProperty,
                                                    const std::string& include,
                                                    std::string* error) const {
    const ParameterDefinition* definition = findParameterDefinition(name, ParameterClassification::PROPERTY, true);
    if (!definition) {
        if (error) {
            *error = "Property not found";
        }
        return false;
    }

    outProperty = buildRegistryPropertyJson(*definition,
                                            includeTokenEnabled(include, "schema"),
                                            includeTokenEnabled(include, "value"));
    return true;
}

int CameraPropertyService::getPropertyValueString(const std::string& name,
                                                  std::string& value,
                                                  std::string* error) const {
    if (hasLegacyProperty(name)) {
        value = jsonValueToString(readValue(name));
        return 0;
    }

    const ParameterDefinition* definition = findParameterDefinition(name, ParameterClassification::PROPERTY, true);
    if (!definition) {
        if (error) {
            *error = "Property not found";
        }
        return -1;
    }

    value = jsonValueToString(readRegistryValue(*definition));
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

bool CameraPropertyService::validateRegistryValue(const ParameterDefinition& definition,
                                                  const Json::Value& value,
                                                  Json::Value& normalized,
                                                  std::string& error,
                                                  bool enforceEnabled) const {
    if (definition.permission == ParameterPermission::READ ||
        definition.permission == ParameterPermission::FACTORY ||
        definition.permission == ParameterPermission::COMMAND) {
        error = "Property is readonly";
        return false;
    }

    if (enforceEnabled) {
        std::string disabledReason;
        if (!isParameterEnabled(definition, &disabledReason)) {
            error = "Property disabled: " + disabledReason;
            return false;
        }
    }

    if (definition.type == ParameterValueType::BOOLEAN) {
        bool parsed = false;
        if (!parseBoolValue(value, parsed)) {
            error = "Invalid boolean value";
            return false;
        }
        normalized = parsed;
        return true;
    }

    if (definition.type == ParameterValueType::NUMBER) {
        int parsed = 0;
        if (!parseIntValue(value, parsed)) {
            error = "Invalid integer value";
            return false;
        }

        if (!definition.options.empty()) {
            for (const auto& option : definition.options) {
                if (option.isInt() && option.asInt() == parsed) {
                    normalized = parsed;
                    return true;
                }
            }
            error = "Invalid value";
            return false;
        }

        if (definition.range.enabled) {
            if (parsed < definition.range.min || parsed > definition.range.max) {
                error = "Value out of range";
                return false;
            }
            if (((parsed - definition.range.min) % definition.range.step) != 0) {
                error = "Value does not match step";
                return false;
            }
        }

        normalized = parsed;
        return true;
    }

    if (definition.type == ParameterValueType::STRING_ARRAY) {
        if (!value.isArray()) {
            error = "Invalid array value";
            return false;
        }
        normalized = value;
        return true;
    }

    std::string parsed = value.isString() ? value.asString() : jsonValueToString(value);
    if (!definition.options.empty()) {
        for (const auto& option : definition.options) {
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

int CameraPropertyService::writeRegistryValue(const ParameterDefinition& definition,
                                              const Json::Value& normalized,
                                              std::string* error) {
    if (definition.storage.kind == ParameterStorageKind::PLACEHOLDER ||
        definition.storage.kind == ParameterStorageKind::COMPUTED ||
        definition.storage.kind == ParameterStorageKind::COMMAND ||
        definition.storage.kind == ParameterStorageKind::NONE) {
        if (error) {
            *error = "Storage binding is not implemented in first pass";
        }
        return -1;
    }

    if (definition.storage.kind == ParameterStorageKind::DEVICE_CONFIG) {
        auto deviceConfig = DeviceConfig::getInstance();
        if (definition.type == ParameterValueType::NUMBER || definition.type == ParameterValueType::BOOLEAN) {
            int value = 0;
            if (!parseIntValue(normalized, value)) {
                if (error) {
                    *error = "Invalid integer value";
                }
                return -1;
            }
            deviceConfig->set(definition.storage.section, definition.storage.key, value);
        } else {
            deviceConfig->set(definition.storage.section, definition.storage.key, normalized.asString());
        }

        if (!persistDeviceConfig()) {
            if (error) {
                *error = "Failed to persist device config";
            }
            return -1;
        }
        return 0;
    }

    Settings* settings = Settings::getInstance().get();
    const std::string& member = definition.storage.member;

    if (member == "cameraMode") {
        settings->cameraMode = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "stillSize") {
        static const struct { const char* name; int index; } sizeMap[] = {
            {"2M", SNAP_IMG_SIZE_2M}, {"4M", SNAP_IMG_SIZE_4M},
            {"8M", SNAP_IMG_SIZE_8M}, {"16M", SNAP_IMG_SIZE_16M},
            {"24M", SNAP_IMG_SIZE_24M}, {"32M", SNAP_IMG_SIZE_32M},
            {"42M", SNAP_IMG_SIZE_42M},
        };
        std::string name = normalized.asString();
        bool found = false;
        for (const auto& entry : sizeMap) {
            if (name == entry.name) {
                settings->stillSize = static_cast<uint8_t>(entry.index);
                found = true;
                break;
            }
        }
        if (!found) {
            if (error) {
                *error = "Unsupported image size";
            }
            return -1;
        }
    } else if (member == "stillQuality") {
        int val = normalized.asInt();
        if (val < 1 || val > 3) { if (error) *error = "Invalid image quality (1-3)"; return -1; }
        settings->stillQuality = static_cast<uint8_t>(val);
    } else if (member == "burstNumber") {
        settings->burstNumber = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "shootingInterval") {
        int val = normalized.asInt();
        if (val < 100 || val > 2000 || (val % 100) != 0) {
            if (error) *error = "Invalid shooting interval (100-2000, step 100)";
            return -1;
        }
        settings->shootingInterval = static_cast<uint8_t>(val / 100);
    } else if (member == "videoSize") {
        int width = 0;
        int height = 0;
        int fps = 0;
        if (!parseSpecVideoMode(normalized.asString(), width, height, fps)) {
            if (error) {
                *error = "Invalid video size";
            }
            return -1;
        }
        const std::vector<VideoMode> modes = buildSupportedVideoModes();
        const VideoMode* targetMode = findModeByResolutionFps(modes, width, height, fps);
        if (!targetMode) {
            if (error) {
                *error = "Unsupported video size";
            }
            return -1;
        }
        settings->videoSize = static_cast<uint8_t>(targetMode->index);
    } else if (member == "bitrate") {
        const std::vector<VideoMode> modes = buildSupportedVideoModes();
        writeBitrateForMode(getCurrentVideoMode(modes), normalized.asInt());
    } else if (member == "videoCodec") {
        int val = normalized.asInt();
        if (val < 1 || val > 2) { if (error) *error = "Invalid codec value (1=H.264, 2=H.265)"; return -1; }
        settings->videoCodec = static_cast<uint8_t>(val);
    } else if (member == "videoRcMode") {
        int val = normalized.asInt();
        if (val < 1 || val > 4) { if (error) *error = "Invalid RC mode (1=CBR, 2=VBR, 3=CVBR, 4=SMART)"; return -1; }
        settings->videoRcMode = static_cast<uint8_t>(val);
    } else if (member == "audioRecordVolume") {
        settings->audioRecordVolume = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "audioRecordGain") {
        settings->audioRecordGain = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "videoLength") {
        writeVideoLengthSeconds(normalized.asInt());
    } else if (member == "pirEn") {
        settings->pirEn = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "ckPirSensitivity") {
        settings->ckPirSensitivity = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "shootingLimits") {
        settings->shootingLimits = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "timerEn") {
        settings->timerEn = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "timerLapse") {
        if (!parseTimeValue(normalized, settings->timerLapse_m, settings->timerLapse_s)) {
            if (error) *error = "Invalid time value (format: MM:SS)";
            return -1;
        }
    } else if (member == "timer1s") {
        if (!parseTimeValue(normalized, settings->timer1s_h, settings->timer1s_m)) {
            if (error) *error = "Invalid time value";
            return -1;
        }
    } else if (member == "timer1e") {
        if (!parseTimeValue(normalized, settings->timer1e_h, settings->timer1e_m)) {
            if (error) *error = "Invalid time value";
            return -1;
        }
    } else if (member == "timer2s") {
        if (!parseTimeValue(normalized, settings->timer2s_h, settings->timer2s_m)) {
            if (error) *error = "Invalid time value";
            return -1;
        }
    } else if (member == "timer2e") {
        if (!parseTimeValue(normalized, settings->timer2e_h, settings->timer2e_m)) {
            if (error) *error = "Invalid time value";
            return -1;
        }
    } else if (member == "timer3s") {
        if (!parseTimeValue(normalized, settings->timer3s_h, settings->timer3s_m)) {
            if (error) *error = "Invalid time value";
            return -1;
        }
    } else if (member == "timer3e") {
        if (!parseTimeValue(normalized, settings->timer3e_h, settings->timer3e_m)) {
            if (error) *error = "Invalid time value";
            return -1;
        }
    } else if (member == "weekRepeats") {
        int value = 0;
        if (!parseIntValue(normalized, value)) {
            if (error) *error = "Invalid repeat value";
            return -1;
        }
        settings->weekRepeats = static_cast<uint8_t>(value);
    } else if (member == "stampEn") {
        settings->stampEn = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "autoCover") {
        settings->autoCover = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "heartRate") {
        int value = normalized.asInt();
        settings->heartRate_0 = static_cast<uint8_t>((value >> 24) & 0xFF);
        settings->heartRate_1 = static_cast<uint8_t>((value >> 16) & 0xFF);
        settings->heartRate_2 = static_cast<uint8_t>((value >> 8) & 0xFF);
        settings->heartRate_3 = static_cast<uint8_t>(value & 0xFF);
    } else if (member == "remote_wakeup") {
        settings->remote_wakeup = static_cast<uint8_t>(normalized.asInt());
    } else if (member == "devName") {
        std::strncpy(settings->devName, normalized.asString().c_str(), sizeof(settings->devName) - 1);
        settings->devName[sizeof(settings->devName) - 1] = '\0';
    } else if (member == "timezone") {
        // R_timezone_format: normalize to UTC-prefixed form (Timezone::setTimezone
        // needs "UTC+"/"UTC-" for POSIX sign flip; registry default is "+8" shorthand).
        std::string tz = normalized.asString();
        if (tz.find("UTC") == std::string::npos && !tz.empty()) {
            tz = "UTC" + tz;
        }
        settings->timezone = tz;
    } else if (member == "upid") {
        settings->upid = normalized.asString();
    } else if (member == "upwd") {
        settings->upwd = normalized.asString();
    } else if (member == "lowVoltage") {
        settings->lowVoltage = normalized.asString();
    } else if (member == "endVoltage") {
        settings->endVoltage = normalized.asString();
    } else {
        if (error) {
            *error = "Storage binding is not implemented in first pass";
        }
        return -1;
    }

    if (!persistSettings()) {
        if (error) {
            *error = "Failed to persist settings";
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
    if (!hasLegacyProperty(name)) {
        return setRegistryPropertyValue(name, value, outProperty, error);
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

int CameraPropertyService::setRegistryPropertyValue(const std::string& name,
                                                    const Json::Value& value,
                                                    Json::Value* outProperty,
                                                    std::string* error) {
    const ParameterDefinition* definition = findParameterDefinition(name, ParameterClassification::PROPERTY, true);
    if (!definition) {
        if (error) {
            *error = "Property not found";
        }
        return -1;
    }

    Json::Value normalized;
    std::string validateError;
    if (!validateRegistryValue(*definition, value, normalized, validateError)) {
        if (error) {
            *error = validateError;
        }
        return -1;
    }

    int ret = writeRegistryValue(*definition, normalized, error);
    if (ret == 0 && outProperty) {
        *outProperty = buildRegistryPropertyJson(*definition, true, true);
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

    if (!names.empty()) {
        for (const auto& name : names) {
            if (hasLegacyProperty(name)) {
                continue;
            }

            const ParameterDefinition* definition = findParameterDefinition(name, ParameterClassification::PROPERTY, true);
            if (!definition) {
                if (error) {
                    *error = "Property not found";
                }
                return -1;
            }

            Json::Value propertyJson;
            std::string localError;
            if (setRegistryPropertyValue(name, definition->defaultValue, &propertyJson, &localError) != 0) {
                if (error) {
                    *error = localError;
                }
                return -1;
            }
            appliedProperties[definition->rawName] = propertyJson["value"];
            resetCount++;
        }
    }

    return resetCount;
}

int CameraPropertyService::resetFactoryProperties(const std::string& group,
                                                  const std::vector<std::string>& names,
                                                  Json::Value& result,
                                                  std::string* error) {
    const std::string requestedGroup = group.empty() ? "all" : group;
    const std::string selectedGroup = requestedGroup == "all" ? "all" : canonicalPropertyGroup(requestedGroup);

    result = Json::Value(Json::objectValue);
    result["group"] = selectedGroup;

    Json::Value results(Json::arrayValue);
    std::vector<const ParameterDefinition*> targets;
    std::set<std::string> seenTargets;
    int applied = 0;
    int skipped = 0;
    int failed = 0;

    auto appendTarget = [&](const ParameterDefinition& definition) {
        if (definition.classification != ParameterClassification::PROPERTY) {
            return;
        }
        if (seenTargets.insert(definition.id).second) {
            targets.push_back(&definition);
        }
    };

    auto appendMissing = [&](const std::string& name, const std::string& reason) {
        Json::Value item(Json::objectValue);
        item["name"] = name;
        item["status"] = "failed";
        item["reason"] = reason;
        results.append(item);
        failed++;
    };

    if (names.empty()) {
        if (selectedGroup == "all") {
            for (const auto& currentGroup : specPropertyGroupOrder()) {
                for (const auto& definition : getCameraParameterDefinitions()) {
                    if (definition.classification == ParameterClassification::PROPERTY &&
                        equalsIgnoreCase(definition.group, currentGroup)) {
                        appendTarget(definition);
                    }
                }
            }
        } else {
            for (const auto& definition : getCameraParameterDefinitions()) {
                if (definition.classification == ParameterClassification::PROPERTY &&
                    equalsIgnoreCase(definition.group, selectedGroup)) {
                    appendTarget(definition);
                }
            }
        }
    } else {
        for (const auto& name : names) {
            if (name.empty()) {
                appendMissing(name, "empty_name");
                continue;
            }

            const ParameterDefinition* definition = findParameterDefinition(name,
                                                                            ParameterClassification::PROPERTY,
                                                                            true);
            if (!definition) {
                appendMissing(name, "property_not_found");
                continue;
            }
            appendTarget(*definition);
        }
    }

    for (const auto* definition : targets) {
        Json::Value item(Json::objectValue);
        item["id"] = definition->id;
        item["name"] = definition->rawName;
        item["raw_name"] = definition->rawName;
        item["group"] = definition->group;
        item["default_value"] = definition->defaultValue;
        item["storage_kind"] = parameterStorageKindToString(definition->storage.kind);

        if (definition->permission == ParameterPermission::READ ||
            definition->permission == ParameterPermission::FACTORY ||
            definition->permission == ParameterPermission::COMMAND) {
            item["status"] = "skipped";
            item["reason"] = "readonly";
            skipped++;
            results.append(item);
            continue;
        }

        if (!isWritableRegistryStorage(*definition)) {
            item["status"] = "skipped";
            item["reason"] = "storage_not_implemented";
            skipped++;
            results.append(item);
            continue;
        }

        Json::Value normalized;
        std::string validateError;
        if (!validateRegistryValue(*definition,
                                   definition->defaultValue,
                                   normalized,
                                   validateError,
                                   false)) {
            item["status"] = "failed";
            item["reason"] = "validation_failed";
            item["error"] = validateError;
            failed++;
            results.append(item);
            continue;
        }

        std::string writeError;
        if (writeRegistryValue(*definition, normalized, &writeError) != 0) {
            item["status"] = "failed";
            item["reason"] = "write_failed";
            item["error"] = writeError.empty() ? "Failed to reset property" : writeError;
            failed++;
            results.append(item);
            continue;
        }

        item["status"] = "applied";
        item["applied_value"] = readRegistryValue(*definition);
        applied++;
        results.append(item);
    }

    result["total"] = static_cast<int>(results.size());
    result["applied"] = applied;
    result["skipped"] = skipped;
    result["failed"] = failed;
    result["results"] = results;

    if (failed > 0 && error) {
        *error = "One or more properties failed to reset";
    }

    return applied;
}

void CameraPropertyService::getVideoRecordConfig(int& width, int& height, int& fps, int& bitrateKbps) const {
    const std::vector<VideoMode> modes = buildSupportedVideoModes();
    const VideoMode& currentMode = getCurrentVideoMode(modes);
    width = currentMode.width;
    height = currentMode.height;
    fps = currentMode.fps;
    bitrateKbps = currentBitrateKbpsForMode(currentMode);
}

int CameraPropertyService::getVideoRecordLength() const {
    return currentVideoLengthSeconds();
}

int CameraPropertyService::getVideoRecordCodec() const {
    Settings* settings = Settings::getInstance().get();
    int codec = static_cast<int>(settings->videoCodec);
    if (codec < 1 || codec > 2) codec = 1;  // default H.264
    return codec;
}

int CameraPropertyService::getVideoRecordRcMode() const {
    Settings* settings = Settings::getInstance().get();
    int mode = static_cast<int>(settings->videoRcMode);
    if (mode < 1 || mode > 4) mode = 1;  // default CBR
    return mode;
}

int CameraPropertyService::getStillQualityForJpeg() const {
    int level = static_cast<int>(Settings::getInstance()->stillQuality);
    switch (level) {
        case 1:  return 60;   // Economy
        case 2:  return 80;   // Normal
        case 3:  return 95;   // Fine
        default: return 85;   // fallback
    }
}

} // namespace service
