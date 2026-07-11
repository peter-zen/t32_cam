#pragma once

#include "CameraParameterRegistry.h"

#include <json/json.h>

#include <string>

namespace service {

struct CameraFactoryImportResult {
    bool success = false;
    bool restartRequired = false;
    int appliedCount = 0;
    std::string selectedInput;
    std::string decision;
    Json::Value errors = Json::Value(Json::arrayValue);
};

class CameraFactoryConfigImporter {
public:
    static constexpr const char* kJsonFileName = "camera_factory_config.json";

    CameraFactoryImportResult importFromSdRoot(const std::string& sdRoot) const;
    bool validateFactoryConfigJson(const Json::Value& root, Json::Value& errors) const;

private:
    bool loadJsonFile(const std::string& path, Json::Value& root, std::string& error) const;
    bool fileExists(const std::string& path) const;
    bool validateConfigSection(const Json::Value& sectionRoot,
                               const std::string& classification,
                               Json::Value& errors) const;
    bool isWritableStorage(const ParameterDefinition& definition) const;
};

} // namespace service
