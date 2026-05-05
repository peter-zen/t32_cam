#pragma once

#include "CameraParameterRegistry.h"

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
    Json::Value getPropertiesJson(const std::string& group = "all",
                                  const std::string& include = "schema,value") const;
    bool getPropertyJson(const std::string& name, Json::Value& outProperty, std::string* error = nullptr) const;
    bool getRegistryPropertyJson(const std::string& name,
                                 Json::Value& outProperty,
                                 const std::string& include = "schema,value",
                                 std::string* error = nullptr) const;
    int getPropertyValueString(const std::string& name, std::string& value, std::string* error = nullptr) const;

    int setPropertyValue(const std::string& name,
                         const Json::Value& value,
                         Json::Value* outProperty = nullptr,
                         std::string* error = nullptr);
    int setRegistryPropertyValue(const std::string& name,
                                 const Json::Value& value,
                                 Json::Value* outProperty = nullptr,
                                 std::string* error = nullptr);

    int resetProperties(const std::vector<std::string>& names,
                        Json::Value& appliedProperties,
                        std::string* error = nullptr);
    int resetFactoryProperties(const std::string& group,
                               const std::vector<std::string>& names,
                               Json::Value& result,
                               std::string* error = nullptr);

    void getVideoRecordConfig(int& width, int& height, int& fps, int& bitrateKbps) const;
    int getVideoRecordLength() const;
    int getVideoRecordCodec() const;   // returns 1=H.264, 2=H.265 (registry value)
    int getVideoRecordRcMode() const;  // returns 1=CBR, 2=VBR, 3=CVBR, 4=SMART (registry value)
    int getStillQualityForJpeg() const; // maps registry 1-3 quality level to JPEG encoder 1-100 scale

private:
    CameraPropertyService() = default;

    bool hasLegacyProperty(const std::string& name) const;
    bool hasProperty(const std::string& name) const;
    CameraPropertySchema buildSchema(const std::string& name) const;
    Json::Value getDefaultValue(const std::string& name) const;
    Json::Value readValue(const std::string& name) const;
    Json::Value buildPropertyJson(const std::string& name, bool includeName) const;
    Json::Value buildRegistryPropertyJson(const ParameterDefinition& definition,
                                          bool includeSchema,
                                          bool includeValue) const;
    Json::Value readRegistryValue(const ParameterDefinition& definition) const;
    bool isParameterEnabled(const ParameterDefinition& definition,
                            std::string* disabledReason = nullptr) const;

    bool validateValue(const CameraPropertySchema& schema,
                       const Json::Value& value,
                       Json::Value& normalized,
                       std::string& error) const;
    bool validateRegistryValue(const ParameterDefinition& definition,
                               const Json::Value& value,
                               Json::Value& normalized,
                               std::string& error,
                               bool enforceEnabled = true) const;

    int writeValue(const std::string& name, const Json::Value& normalized, std::string* error);
    int writeRegistryValue(const ParameterDefinition& definition,
                           const Json::Value& normalized,
                           std::string* error);
    bool persistSettings() const;
    bool persistDeviceConfig() const;
};

} // namespace service
