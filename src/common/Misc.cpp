#include <sys/stat.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <dirent.h>
#include "json/json.h"
#include "Misc.h"
#include "Logger.h"
#include "system_call.h"

std::string Misc::netifname;
bool Misc::syscall_inited = false;
std::mutex Misc::syscall_mutex;

std::string Misc::getFilename(const std::string &pathname)
{
	auto pos = pathname.find_last_of("/");
	if (pos == std::string::npos) {
		return "";
	}
	return pathname.substr(pos + 1);
}

std::string Misc::getFilepath(const std::string &pathname)
{
	auto pos = pathname.find_last_of("/");
	if (pos == std::string::npos) {
		return "";
	}
	return pathname.substr(0, pos);
}

std::string Misc::getFileType(const std::string &pathname)
{
	auto pos = pathname.find_last_of(".");
	if (pos == std::string::npos) {
		return "";
	}
	return pathname.substr(pos + 1);
}

size_t Misc::getFileSize(const std::string &file_pathname)
{
	struct stat st;
	if (stat(file_pathname.c_str(), &st) == 0) {
		return st.st_size;
	}

	return 0;
}
bool Misc::copyFile(const std::string &src_pathname, const std::string &dst_pathname)
{
	int ret;
	{
		std::unique_lock<std::mutex> lock(syscall_mutex);
		if (!syscall_inited) {
			ret = system_call_init();
			if(ret < 0) {
				Logger::log(LogLevel::ERROR, "system_call_init failed");
				return false;
			}
			syscall_inited = true;
		}
	}
	
	std::string command = "cp -rf " + src_pathname + " " + dst_pathname;
	system_call((char*)command.c_str(), 10000);
	if(ret < 0) {
		return false;
	}
	
	return true;
}

bool Misc::moveFile(const std::string &src_pathname, const std::string &dst_pathname)
{
	int ret;
	{
		std::unique_lock<std::mutex> lock(syscall_mutex);
		if (!syscall_inited) {
			ret = system_call_init();
			if(ret < 0) {
				Logger::log(LogLevel::ERROR, "system_call_init failed");
				return false;
			}
			syscall_inited = true;
		}
	}
	
	std::string command = "mv -f " + src_pathname + " " + dst_pathname;
	system_call((char*)command.c_str(), 10000);
	if(ret < 0) {
		return false;
	}
	
	return true;
}
bool Misc::isJsonFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    Json::Value root;
    Json::CharReaderBuilder readerBuilder;
    std::string errs;
    bool parsingSuccessful = Json::parseFromStream(readerBuilder, file, &root, &errs);
    file.close();

    return parsingSuccessful;
}

bool Misc::deleteFile(const std::string& filePath)
{
    if (remove(filePath.c_str()) != 0) {
        Logger::log(LogLevel::ERROR, "Failed to delete file %s", filePath.c_str());
        return false;
    }
    return true;
}

std::vector<std::string> Misc::listFilenames(const std::string& dirname)
{
    std::vector<std::string> fileNames;
    DIR* dir = opendir(dirname.c_str());
    if (dir == nullptr) {
        Logger::log(LogLevel::ERROR, "Failed to open directory %s", dirname.c_str());
        return fileNames;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (entry->d_type == DT_REG) {
            fileNames.push_back(entry->d_name);
        }
    }

    closedir(dir);
    return fileNames;
}

bool Misc::createDirectory(const std::string& path, mode_t mode)
{
    std::string tempPath;
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!tempPath.empty()) {
                int result = mkdir(tempPath.c_str(), mode);
                if (result == -1 && errno != EEXIST) {
                    return false;
                }
            }
            tempPath += c;
        } else {
            tempPath += c;
        }
    }
    if (!tempPath.empty()) {
        int result = mkdir(tempPath.c_str(), mode);
        if (result == -1 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

std::string Misc::getIPAddress(const std::string &interface_name)
{
	struct ifaddrs *ifaddr, *ifa;
	int family;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) == -1) {
		Logger::log(LogLevel::ERROR, "Error getting network interfaces");
		return "";
	}

	for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == nullptr)
			continue;

		family = ifa->ifa_addr->sa_family;
		if (family == AF_INET && interface_name == ifa->ifa_name) {
			if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, nullptr, 0,
					NI_NUMERICHOST) == 0) {
				freeifaddrs(ifaddr);
				return std::string(host);
			}
		}
	}

	freeifaddrs(ifaddr);
	return "";
}

std::string Misc::getGatewayAddress(const std::string &interface_name)
{
    std::ifstream route_file("/proc/net/route");
    if (!route_file.is_open()) {
        Logger::log(LogLevel::ERROR, "Failed to open /proc/net/route");
        return "";
    }

    std::string line;
    while (std::getline(route_file, line)) {
        std::istringstream iss(line);
        std::string iface, destination, gateway, flags;
        iss >> iface >> destination >> gateway;

        if (iface == interface_name && destination == "00000000") {
            // Convert hexadecimal gateway address to dotted-decimal notation
            unsigned int gateway_int;
            std::istringstream("0x" + gateway) >> std::hex >> gateway_int;
            struct in_addr addr;
            addr.s_addr = gateway_int;
            return std::string(inet_ntoa(addr));
        }
    }

    return "";
}

std::string Misc::getNetworkInterfaceName()
{ 
	return netifname; 
}

void Misc::setNetworkInterfaceName(std::string name)
{ 
	netifname = name;
}

bool Misc::connectWifi(const std::string &ssid, const std::string &password)
{
	int ret;
	{
		std::unique_lock<std::mutex> lock(syscall_mutex);
		if (!syscall_inited) {
			ret = system_call_init();
			if(ret < 0) {
				Logger::log(LogLevel::ERROR, "system_call_init failed");
				return false;
			}
			std::string command = "insmod_mmc";
			ret = system_call((char*)command.c_str(), 1000);
			if(ret < 0) {
				system_call_exit();
				Logger::log(LogLevel::ERROR, "%s error", command);
				return false;
			}

			command = "insmod /system/cywdhd.ko firmware_path=/system/cyfmac43012-sdio.bin nvram_path=/system/cyfmac43012-sdio.txt clm_path=/system/cyfmac43012-sdio.clm_blob";
			ret = system_call((char*)command.c_str(), 1000);
			if(ret < 0) {
				system_call_exit();
				Logger::log(LogLevel::ERROR, "%s error", command);
				return false;
			}
			syscall_inited = true;
		}
	}
	
	Logger::log(LogLevel::INFO, "Connecting to WiFi: %s ", ssid.c_str());
    std::string command = "speedy --wifi_ssid " + ssid + " --wifi_pass " + password;
    ret = system_call((char*)command.c_str(), 10000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "Connect to WiFi: %s error", ssid.c_str());
			return false;
	}
	Logger::log(LogLevel::INFO, "Connecting to WiFi: %s done", ssid.c_str());
    return true;
}

bool Misc::startDHCP()
{
	int ret;
	{
		std::unique_lock<std::mutex> lock(syscall_mutex);
		if (!syscall_inited) {
			ret = system_call_init();
			if(ret < 0) {
				Logger::log(LogLevel::ERROR, "system_call_init failed");
				return false;
			}
			syscall_inited = true;
		}
	}
	
	std::string command = "udhcpc -i " + netifname;
	system_call((char*)command.c_str(), 10000);
	Logger::log(LogLevel::INFO, "dhcp done");
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "dhcp on %s error", netifname.c_str());
			return false;
	}
	
	return true;
}

bool Misc::mountSDCard(const std::string& target_path)
{
	int ret;
	if (!createDirectory(target_path)) {
		return false;
	}

	{
		std::unique_lock<std::mutex> lock(syscall_mutex);
		if (!syscall_inited) {
			ret = system_call_init();
			if(ret < 0) {
				Logger::log(LogLevel::ERROR, "system_call_init failed");
				return false;
			}
			syscall_inited = true;
		}
	}

	std::string command = "mount /dev/mmcblk0p1 " + target_path;
	ret = system_call((char*)command.c_str(), 5000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "mount sdcard to %s error", target_path.c_str());
			return false;
	}
	
	return true;
}