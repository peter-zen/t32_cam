#include "DeviceConfig.h"
#include <cerrno>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <json/json.h>
#include "EnvManager.h"

std::shared_ptr<DeviceConfig> DeviceConfig::getInstance()
{
    static std::shared_ptr<DeviceConfig> instance = nullptr;
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new DeviceConfig()); });
    return instance;
}

DeviceConfig::DeviceConfig()
{
    config_filename = EnvManager::getInstance()->getEnv("CONFIG_FILE");
    parse(config_filename);
    permit_flush = true;
}

DeviceConfig::~DeviceConfig()
{
    flush();
}

bool DeviceConfig::parse(const std::string &configFile)
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

    /* Walk section -> key -> leaf. config_data stays a string bag: every leaf
       is read via .asString() so get(int)'s istringstream>>int path and
       get(string)'s direct return are unchanged from the ini backend. */
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

void DeviceConfig::flush_control(bool permit)
{
    std::lock_guard<std::mutex> lock(config_mutex);
    permit_flush = permit;
}

bool DeviceConfig::flush()
{
    std::lock_guard<std::mutex> lock(config_mutex);
    if (!permit_flush) {
        return false;
    }

    /* Build a JSON object from the string bag (all leaves are strings -- D2:
       numeric values like MSPort="8899" are stored and written as strings so
       the ini/json backends stay byte-for-byte equivalent at the bag level). */
    Json::Value root(Json::objectValue);
    for (const auto &section : config_data) {
        Json::Value &secNode = root[section.first];
        secNode = Json::objectValue;
        for (const auto &keyValue : section.second) {
            secNode[keyValue.first] = keyValue.second;
        }
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "    ";
    std::string doc = Json::writeString(writer, root);

    /* Atomic write: emit to <filename>.tmp then rename over the target. Same-
       partition rename is POSIX-atomic, repairing the old std::ofstream non-
       atomic overwrite (design config-ini-to-json-migration.md §8 power-loss
       hardening). */
    std::string tmpPath = config_filename + ".tmp";
    {
        std::ofstream tmp(tmpPath, std::ios::binary | std::ios::trunc);
        if (!tmp.is_open()) {
            return false;
        }
        tmp << doc;
        tmp.flush();
        if (!tmp.good()) {
            tmp.close();
            std::remove(tmpPath.c_str());
            return false;
        }
    }
    if (std::rename(tmpPath.c_str(), config_filename.c_str()) != 0) {
        // R4 (T23): rename fails with EXDEV when tmp and target are on different
        // filesystems (e.g. /tmp tmpfs -> /config jffs2). Fall back to copy+
        // unlink, which is NOT atomic across crash but is the best-effort
        // cross-filesystem path.
        if (errno != EXDEV) {
            std::remove(tmpPath.c_str());
            return false;
        }
        if (!copyFile(tmpPath, config_filename)) {
            std::remove(tmpPath.c_str());
            return false;
        }
        std::remove(tmpPath.c_str());
        return true;
    }
    return true;
}

bool DeviceConfig::copyFile(const std::string &src, const std::string &dst)
{
    std::ifstream in(src, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    out << in.rdbuf();
    out.flush();
    return out.good();
}

int DeviceConfig::get(const std::string &section, const std::string &key,
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

std::string DeviceConfig::get(const std::string &section, const std::string &key,
                                           const char* default_value)
{
    return get(section, key, std::string(default_value));
}

std::string DeviceConfig::get(const std::string &section, const std::string &key,
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

void DeviceConfig::set(const std::string &section, const std::string &key,
                            const int &value)
{
    std::stringstream ss;
    ss << value;
    std::string str = ss.str();
    std::lock_guard<std::mutex> lock(config_mutex);
    config_data[section][key] = str;
}

void DeviceConfig::set(const std::string &section, const std::string &key,
                                    const char* const &value)
{
    std::lock_guard<std::mutex> lock(config_mutex);
    config_data[section][key] = value;
}

void DeviceConfig::set(const std::string &section, const std::string &key,
                                    const std::string &value)
{
    std::lock_guard<std::mutex> lock(config_mutex);
    config_data[section][key] = value;
}