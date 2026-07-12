#include "ProductConfig.h"
#include <fstream>
#include <json/json.h>
#include <sstream>
#include "EnvManager.h"
#include "Logger.h"

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

    // T28 — top-level "capabilities" is a flat string (not a nested section),
    // so the two-level walk above skips it. Extract it explicitly.
    // Design um-capability-advertising §3.7: absent/empty -> fail-safe
    // {"um_live"} (lean toward the thin direction on 64MB T32).
    caps_set_.clear();
    if (root.isMember("capabilities") && root["capabilities"].isString()) {
        parseCapabilities(root["capabilities"].asString());
    }
    if (caps_set_.empty()) {
        Logger::log(LogLevel::WARNING,
                    "ProductConfig: capabilities absent/empty -> fail-safe {um_live}");
        caps_set_.insert("um_live");
    }
    return true;
}

void ProductConfig::parseCapabilities(const std::string &raw)
{
    // Comma-separated presence-set: "um_live,um_snap,um_rec,um_pb". Tokens are
    // trimmed; empty tokens skipped. Unknown tokens are retained (forward-compat:
    // old firmware ignores new tokens but keeps them so self-report stays honest)
    // with a WARNING (design §3.9).
    static const std::set<std::string> kKnown = {
        "um_live", "um_snap", "um_rec", "um_pb"
    };
    std::stringstream ss(raw);
    std::string item;
    while (std::getline(ss, item, ',')) {
        // trim whitespace
        size_t b = item.find_first_not_of(" \t");
        size_t e = item.find_last_not_of(" \t");
        if (b == std::string::npos) continue;  // all-whitespace / empty token
        std::string tok = item.substr(b, e - b + 1);
        if (kKnown.count(tok) == 0) {
            Logger::log(LogLevel::WARNING,
                        "ProductConfig: unknown capability token '%s' (retained, forward-compat)",
                        tok.c_str());
        }
        caps_set_.insert(tok);
    }
}

void ProductConfig::reload()
{
    std::lock_guard<std::mutex> lock(config_mutex);
    config_data.clear();
    caps_set_.clear();
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

bool ProductConfig::hasCap(const std::string &token) const
{
    std::lock_guard<std::mutex> lock(config_mutex);
    return caps_set_.count(token) > 0;
}

std::set<std::string> ProductConfig::getCaps() const
{
    std::lock_guard<std::mutex> lock(config_mutex);
    return caps_set_;
}
