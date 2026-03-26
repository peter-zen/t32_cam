#pragma once

#include <json/json.h>

#include <string>
#include <vector>

namespace service {

enum class CameraPropertyType {
    ENUM,
    INTEGER,
    BOOLEAN,
};

struct CameraPropertyRange {
    bool enabled = false;
    int min = 0;
    int max = 0;
    int step = 1;
};

struct CameraPropertySchema {
    std::string name;
    std::string displayName;
    CameraPropertyType type = CameraPropertyType::ENUM;
    bool readonly = false;
    bool persistent = true;
    std::vector<Json::Value> options;
    CameraPropertyRange range;
    std::string unit;
    Json::Value defaultValue;
};

class CameraPropertyService {
public:
    static CameraPropertyService& getInstance();

    std::vector<std::string> getPropertyNames() const;
    Json::Value getAllPropertiesJson() const;
    bool getPropertyJson(const std::string& name, Json::Value& outProperty, std::string* error = nullptr) const;
    int getPropertyValueString(const std::string& name, std::string& value, std::string* error = nullptr) const;

    int setPropertyValue(const std::string& name,
                         const Json::Value& value,
                         Json::Value* outProperty = nullptr,
                         std::string* error = nullptr);

    int resetProperties(const std::vector<std::string>& names,
                        Json::Value& appliedProperties,
                        std::string* error = nullptr);

    void getVideoRecordConfig(int& width, int& height, int& fps, int& bitrateKbps) const;

private:
    CameraPropertyService() = default;

    bool hasProperty(const std::string& name) const;
    CameraPropertySchema buildSchema(const std::string& name) const;
    Json::Value getDefaultValue(const std::string& name) const;
    Json::Value readValue(const std::string& name) const;
    Json::Value buildPropertyJson(const std::string& name, bool includeName) const;

    bool validateValue(const CameraPropertySchema& schema,
                       const Json::Value& value,
                       Json::Value& normalized,
                       std::string& error) const;

    int writeValue(const std::string& name, const Json::Value& normalized, std::string* error);
    bool persistSettings() const;
    bool persistDeviceConfig() const;
};

} // namespace service
