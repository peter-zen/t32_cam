#include "ProductConfig.h"
#include <fstream>
#include <json/json.h>
#include "EnvManager.h"

std::shared_ptr<ProductConfig> ProductConfig::getInstance()
{
    static std::shared_ptr<ProductConfig> instance = nullptr;
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new ProductConfig()); });
    return instance;
}

ProductConfig::ProductConfig()
{
    config_filename = EnvManager::getInstance()->getEnv("PRODUCT_FILE");
    load(config_filename);
}

bool ProductConfig::load(const std::string &configFile)
{
    std::ifstream file(configFile);
    if (!file.is_open()) {
        return false;
    }

    Json::CharReaderBuilder builder;
    Json::Value root;
    std::string errs;
    if (!Json::parseFromStream(builder, file, &root, &errs)) {
        return false;
    }

    /* Walk section -> key -> leaf. config_data stays a string bag (same shape as
       DeviceConfig) so get(int)'s istringstream>>int path and get(string)'s
       direct return are byte-identical to the pre-migration DeviceConfig reads
       these fields used to go through. */
    for (const auto &section : root.getMemberNames()) {
        const Json::Value &secNode = root[section];
        if (secNode.type() != Json::objectValue) {
            continue;
        }
        for (const auto &key : secNode.getMemberNames()) {
            config_data[section][key] = secNode[key].asString();
        }
    }
    return true;
}

void ProductConfig::reload()
{
    std::lock_guard<std::mutex> lock(config_mutex);
    config_data.clear();
    load(config_filename);
}

int ProductConfig::get(const std::string &section, const std::string &key,
                       int default_value)
{
    std::lock_guard<std::mutex> lock(config_mutex);
    auto secIt = config_data.find(section);
    if (secIt != config_data.end()) {
        auto keyIt = secIt->second.find(key);
        if (keyIt != secIt->second.end()) {
            std::istringstream iss(keyIt->second);
            int value;
            iss >> value;
            return value;
        }
    }
    return default_value;
}

std::string ProductConfig::get(const std::string &section, const std::string &key,
                               const char* default_value)
{
    return get(section, key, std::string(default_value));
}

std::string ProductConfig::get(const std::string &section, const std::string &key,
                               std::string default_value)
{
    std::lock_guard<std::mutex> lock(config_mutex);
    auto secIt = config_data.find(section);
    if (secIt != config_data.end()) {
        auto keyIt = secIt->second.find(key);
        if (keyIt != secIt->second.end()) {
            return keyIt->second;
        }
    }
    return default_value;
}
