#include "CameraFactoryConfigImporter.h"

#include "CameraParameterRegistry.h"
#include "CameraPropertyService.h"

#include "../../config/devconf/DeviceConfig.h"
#include "../../config/devconf/ProductConfig.h"
#include "../../config/env/EnvManager.h"

#include <cstdio>
#include <fstream>
#include <json/json.h>
#include <sys/stat.h>

namespace service {

namespace {

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) {
        return name;
    }
    return base.back() == '/' ? base + name : base + "/" + name;
}

// Read-modify-write product.json with a set of section/key deltas collected
// during factory import. PRODUCT-kind fields are the ONLY thing written here —
// ProductConfig has no write surface (read-only by construction), so the factory
// importer writes product.json directly via jsoncpp and then calls
// ProductConfig::reload() to refresh the in-memory bag. Same-partition rename
// for atomicity, mirroring DeviceConfig::flush.
bool writeProductDelta(const Json::Value& productDelta) {
    if (productDelta.empty()) {
        return true;
    }
    const std::string path = EnvManager::getInstance()->getEnv("PRODUCT_FILE");
    if (path.empty()) {
        return false;
    }

    Json::Value root(Json::objectValue);
    {
        std::ifstream in(path);
        if (in.is_open()) {
            Json::CharReaderBuilder reader;
            std::string errs;
            Json::parseFromStream(reader, in, &root, &errs);
        }
    }
    for (const auto& section : productDelta.getMemberNames()) {
        Json::Value& secNode = root[section];
        if (secNode.type() != Json::objectValue) {
            secNode = Json::objectValue;
        }
        const Json::Value& deltaSec = productDelta[section];
        for (const auto& key : deltaSec.getMemberNames()) {
            secNode[key] = deltaSec[key];
        }
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "    ";
    const std::string doc = Json::writeString(writer, root);

    const std::string tmpPath = path + ".tmp";
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << doc;
        out.flush();
        if (!out.good()) {
            out.close();
            std::remove(tmpPath.c_str());
            return false;
        }
    }
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::remove(tmpPath.c_str());
        return false;
    }
    return true;
}

ParameterClassification parseClassification(const std::string& classification) {
    return classification == "factory" ? ParameterClassification::FACTORY : ParameterClassification::PROPERTY;
}

bool validateType(const ParameterDefinition& definition, const Json::Value& value, std::string& error) {
    if (definition.type == ParameterValueType::NUMBER) {
        if (!value.isInt() && !value.isUInt()) {
            error = "expected number";
            return false;
        }
        const int intValue = value.asInt();
        if (!definition.options.empty()) {
            for (const auto& option : definition.options) {
                if (option.isInt() && option.asInt() == intValue) {
                    return true;
                }
            }
            error = "invalid option";
            return false;
        }
        if (definition.range.enabled) {
            if (intValue < definition.range.min || intValue > definition.range.max) {
                error = "value out of range";
                return false;
            }
            if (((intValue - definition.range.min) % definition.range.step) != 0) {
                error = "value does not match step";
                return false;
            }
        }
    } else if (definition.type == ParameterValueType::BOOLEAN) {
        if (!value.isBool() && !value.isInt() && !value.isUInt()) {
            error = "expected boolean";
            return false;
        }
    } else if (definition.type == ParameterValueType::STRING) {
        if (!value.isString()) {
            error = "expected string";
            return false;
        }
        if (!definition.options.empty()) {
            for (const auto& option : definition.options) {
                if (option.isString() && option.asString() == value.asString()) {
                    return true;
                }
            }
            error = "invalid option";
            return false;
        }
    } else if (definition.type == ParameterValueType::STRING_ARRAY) {
        if (!value.isArray()) {
            error = "expected string array";
            return false;
        }
    }
    return true;
}

void appendError(Json::Value& errors,
                 const std::string& classification,
                 const std::string& group,
                 const std::string& name,
                 const std::string& message) {
    Json::Value error(Json::objectValue);
    error["classification"] = classification;
    error["group"] = group;
    error["name"] = name;
    error["error"] = message;
    errors.append(error);
}

} // namespace

bool CameraFactoryConfigImporter::fileExists(const std::string& path) const {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool CameraFactoryConfigImporter::loadJsonFile(const std::string& path,
                                               Json::Value& root,
                                               std::string& error) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        error = "failed to open json file";
        return false;
    }

    Json::CharReaderBuilder reader;
    if (!Json::parseFromStream(reader, file, &root, &error)) {
        return false;
    }
    return true;
}

bool CameraFactoryConfigImporter::isWritableStorage(const ParameterDefinition& definition) const {
    return definition.storage.kind == ParameterStorageKind::DEVICE_CONFIG ||
           definition.storage.kind == ParameterStorageKind::PRODUCT ||
           definition.storage.kind == ParameterStorageKind::SETTINGS;
}

bool CameraFactoryConfigImporter::validateConfigSection(const Json::Value& sectionRoot,
                                                        const std::string& classification,
                                                        Json::Value& errors) const {
    if (sectionRoot.isNull()) {
        return true;
    }
    if (!sectionRoot.isObject()) {
        appendError(errors, classification, "", "", "section must be an object");
        return false;
    }

    bool ok = true;
    const ParameterClassification parsedClassification = parseClassification(classification);
    for (const auto& group : sectionRoot.getMemberNames()) {
        if (!sectionRoot[group].isObject()) {
            appendError(errors, classification, group, "", "group must be an object");
            ok = false;
            continue;
        }

        for (const auto& name : sectionRoot[group].getMemberNames()) {
            const ParameterDefinition* definition =
                findParameterDefinition(name, parsedClassification, false);
            if (!definition || definition->group != group) {
                appendError(errors, classification, group, name, "unknown field");
                ok = false;
                continue;
            }

            std::string typeError;
            if (!validateType(*definition, sectionRoot[group][name], typeError)) {
                appendError(errors, classification, group, name, typeError);
                ok = false;
            }
        }
    }

    return ok;
}

bool CameraFactoryConfigImporter::validateFactoryConfigJson(const Json::Value& root, Json::Value& errors) const {
    errors = Json::Value(Json::arrayValue);
    if (!root.isObject()) {
        appendError(errors, "", "", "", "root must be an object");
        return false;
    }

    bool ok = true;
    ok = validateConfigSection(root["factory"], "factory", errors) && ok;
    ok = validateConfigSection(root["properties"], "property", errors) && ok;
    return ok;
}

CameraFactoryImportResult CameraFactoryConfigImporter::importFromSdRoot(const std::string& sdRoot) const {
    CameraFactoryImportResult result;
    const std::string jsonPath = joinPath(sdRoot, kJsonFileName);

    if (fileExists(jsonPath)) {
        result.selectedInput = jsonPath;
        result.decision = "json_selected";

        Json::Value root;
        std::string error;
        if (!loadJsonFile(jsonPath, root, error)) {
            appendError(result.errors, "factory", "", kJsonFileName, error);
            result.decision = "invalid_json_no_ini_fallback";
            return result;
        }

        if (!validateFactoryConfigJson(root, result.errors)) {
            result.decision = "json_validation_failed_no_ini_fallback";
            return result;
        }

        Json::Value productDelta(Json::objectValue);
        for (const auto& group : root["factory"].getMemberNames()) {
            for (const auto& name : root["factory"][group].getMemberNames()) {
                const ParameterDefinition* definition =
                    findParameterDefinition(name, ParameterClassification::FACTORY, false);
                if (!definition || !isWritableStorage(*definition)) {
                    continue;
                }
                if (definition->storage.kind == ParameterStorageKind::PRODUCT) {
                    // Collect into product.json delta (read-modify-write at loop end).
                    // ProductConfig is read-only by construction; factory import is the
                    // sole writer and bypasses ProductConfig entirely (direct jsoncpp).
                    productDelta[definition->storage.section][definition->storage.key] =
                        (definition->type == ParameterValueType::NUMBER ||
                         definition->type == ParameterValueType::BOOLEAN)
                            ? Json::Value(root["factory"][group][name].asInt())
                            : Json::Value(root["factory"][group][name].asString());
                } else {
                    // DEVICE_CONFIG (PID) and SETTINGS remain on their existing writers.
                    if (definition->type == ParameterValueType::NUMBER ||
                        definition->type == ParameterValueType::BOOLEAN) {
                        DeviceConfig::getInstance()->set(definition->storage.section,
                                                         definition->storage.key,
                                                         root["factory"][group][name].asInt());
                    } else {
                        DeviceConfig::getInstance()->set(definition->storage.section,
                                                         definition->storage.key,
                                                         root["factory"][group][name].asString());
                    }
                }
                result.appliedCount++;
            }
        }

        CameraPropertyService& properties = CameraPropertyService::getInstance();
        for (const auto& group : root["properties"].getMemberNames()) {
            for (const auto& name : root["properties"][group].getMemberNames()) {
                const ParameterDefinition* definition =
                    findParameterDefinition(name, ParameterClassification::PROPERTY, false);
                if (!definition || !isWritableStorage(*definition)) {
                    continue;
                }
                Json::Value ignored;
                std::string setError;
                if (properties.setRegistryPropertyValue(name, root["properties"][group][name], &ignored, &setError) != 0) {
                    appendError(result.errors, "property", group, name, setError);
                    result.success = false;
                    result.decision = "property_apply_failed";
                    return result;
                }
                result.appliedCount++;
            }
        }

        if (!productDelta.empty()) {
            if (!writeProductDelta(productDelta)) {
                appendError(result.errors, "factory", "", "product.json", "failed to persist product.json");
                result.success = false;
                result.decision = "product_persist_failed";
                return result;
            }
            ProductConfig::getInstance()->reload();
        }
        DeviceConfig::getInstance()->flush();
        result.success = true;
        result.restartRequired = true;
        return result;
    }

    result.success = true;
    result.decision = "no_factory_config";
    return result;
}

} // namespace service
