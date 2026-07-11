#ifndef DEVICECONFIG_H
#define DEVICECONFIG_H

#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <sstream>
#include <fstream>

class DeviceConfig {
    public:
        static std::shared_ptr<DeviceConfig> getInstance();
		int get(const std::string &section, const std::string &key,
			int default_value);
		std::string get(const std::string &section, const std::string &key,
			const char* default_value);
		std::string get(const std::string &section, const std::string &key,
			std::string default_value);
		void set(const std::string &section, const std::string &key,
			const int &value);
		void set(const std::string &section, const std::string &key,
			const char* const &value);
		void set(const std::string &section, const std::string &key,
			const std::string &value);
        
        void flush_control(bool permit);
        bool flush();
        ~DeviceConfig();

    private:
        DeviceConfig();
        DeviceConfig(const DeviceConfig &) = delete;
        DeviceConfig &operator=(const DeviceConfig &) = delete;
        bool parse(const std::string &configFile);
        bool copyFile(const std::string &src, const std::string &dst);  // R4 EXDEV fallback

        std::unordered_map<std::string, std::unordered_map<std::string, std::string>> config_data;
        std::mutex config_mutex;
        std::string config_filename;
        bool permit_flush;
};

#endif /* DEVICECONFIG_H */