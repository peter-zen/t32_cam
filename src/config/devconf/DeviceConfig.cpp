#include "DeviceConfig.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
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

    std::string line, currentSection;
    while (std::getline(file, line)) {
        /* remove space */
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue; /* skip comment and empty line */
        }

        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
        } else {
            auto delimiterPos = line.find('=');
            if (delimiterPos != std::string::npos) {
                std::string key = line.substr(0, delimiterPos);
                std::string value =
                    line.substr(delimiterPos + 1);

                /* remove space */
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);

                config_data[currentSection][key] = value;
            }
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

    std::ofstream file(config_filename);
    if (!file.is_open()) {
        return false;
    }

    for (const auto &section : config_data) {
        file << "[" << section.first << "]\n";
        for (const auto &keyValue : section.second) {
            file << keyValue.first << "=" << keyValue.second
                 << "\n";
        }
        file << "\n";
    }

    return true;
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