#include <sys/stat.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <dirent.h>
#include "json/json.h"
#include "Misc.h"
#include "Logger.h"
#include "system_call.h"
#include "StringConvert.h"

std::string Misc::netifname;
std::mutex Misc::syscall_mutex;
bool Misc::syscall_inited = false;
bool Misc::already_insmod_mmc = false;
bool Misc::already_inited_wifi = false;

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
	
	std::string command = "cp -rf " + src_pathname + " " + dst_pathname;
	ret = syscall((char*)command.c_str(), 10000);
	if(ret < 0) {
		return false;
	}
	
	return true;
}

bool Misc::moveFile(const std::string &src_pathname, const std::string &dst_pathname)
{
	int ret;
	
	std::string command = "mv -f " + src_pathname + " " + dst_pathname;
	ret = syscall((char*)command.c_str(), 10000);
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

std::string Misc::getMACAddress(const std::string &interface_name)
{
    if (interface_name.empty()) {
        return "";
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        Logger::log(LogLevel::ERROR, "Failed to create socket for MAC lookup");
        return "";
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface_name.c_str(), IFNAMSIZ - 1);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(fd);
        Logger::log(LogLevel::ERROR, "Failed to get MAC address for %s", interface_name.c_str());
        return "";
    }

    close(fd);

    const unsigned char* mac = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
    char mac_string[18] = {0};
    snprintf(mac_string, sizeof(mac_string),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return std::string(mac_string);
}

std::string Misc::findUsableNetworkInterface(const std::string &preferred_name)
{
    if (!preferred_name.empty() && !getIPAddress(preferred_name).empty()) {
        return preferred_name;
    }

    struct ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == -1) {
        Logger::log(LogLevel::ERROR, "Error getting network interfaces");
        return "";
    }

    std::string fallback;
    for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        std::string candidate = ifa->ifa_name;
        if (!candidate.empty() && !getIPAddress(candidate).empty()) {
            fallback = candidate;
            break;
        }
    }

    freeifaddrs(ifaddr);
    return fallback;
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

	if (!already_inited_wifi) {
#if defined(WIFI_TYPE_CYW43012)
		std::string command = "insmod /system/bin/wifi/cywdhd.ko firmware_path=/system/bin/wifi/cyfmac43012-sdio.bin nvram_path=/system/bin/wifi/cyfmac43012-sdio.txt clm_path=/system/bin/wifi/cyfmac43012-sdio.clm_blob";
#elif defined(WIFI_TYPE_RTL8189FS)
		std::string command = "insmod /system/bin/wifi/8189fs.ko";
#else
		#error "Unknown WiFi type"
#endif
		ret = syscall((char*)command.c_str(), 10000);
		if(ret < 0) {
			Logger::log(LogLevel::ERROR, "%s error", command.c_str());
			return false;
		}
		already_inited_wifi = true;
	}
	int timeout = 15;
	Logger::log(LogLevel::INFO, "Connecting to WiFi: %s ", ssid.c_str());
	#if defined(WIFI_TYPE_CYW43012)
    std::string command = "/system/bin/wifi/speedy --wifi_ssid " + ssid + " --wifi_pass " + password;
	#elif defined(WIFI_TYPE_RTL8189FS)
	std::string command = "/system/bin/wifi/wpa_conn wlan0 " + ssid + " " + password + " " + to_string_custom(timeout) + " " + to_string_custom(1);
	#else
		#error "Unknown WiFi type"
	#endif
    ret = syscall((char*)command.c_str(), timeout*1000 + 5000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "Connect to WiFi: %s error", ssid.c_str());
			return false;
	}
	Logger::log(LogLevel::INFO, "Connecting to WiFi: %s done", ssid.c_str());
    return true;
}

bool Misc::startDHCP(const std::string &ifname)
{
	std::string netif = ifname.empty() ? netifname : ifname;

	int ret;
	
	std::string command = "udhcpc -i " + netif + " -t " + to_string_custom(10);
	ret = syscall((char*)command.c_str(), 20000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "dhcp on %s error", netif.c_str());
			return false;
	}
	
	return true;
}

bool Misc::ntpSync(const std::string& ntp_server)
{
	int ret;

	std::string command = "busybox ntpd -p " + ntp_server;
	ret = syscall((char*)command.c_str(), 10000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "ntp sync error");
			return false;
	}
	
	return true;
}

bool Misc::getDateTime()
{
	int ret;
	
	std::string command = "date";
	ret = syscall((char*)command.c_str(), 10000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "date error");
			return false;
	}
	
	return true;
}

int Misc::syscall(const char *command, int timeout_ms)
{
    if (command == nullptr) {
        return -1;
    }
    int ret = 0;
    {
        std::unique_lock<std::mutex> lock(syscall_mutex);
        if (!syscall_inited) {
            ret = system_call_init();
            if (ret < 0) {
                return ret;
            }
            syscall_inited = true;
        }
    }
    ret = system_call((char*)command, timeout_ms);
    return ret;
}

int Misc::popencall(char *cmd, char *out, int max_size, int timeout_ms)
{
    if (cmd == nullptr || out == nullptr || max_size <= 0) {
        return -1;
    }
    int ret = 0;
    {
        std::unique_lock<std::mutex> lock(syscall_mutex);
        if (!syscall_inited) {
            ret = system_call_init();
            if (ret < 0) {
                return ret;
            }
            syscall_inited = true;
        }
    }
    ret = popen_call(cmd, out, max_size, timeout_ms);
    return ret;
}

bool Misc::setDateTime(const std::string &date)
{
	int ret;
	
	std::string command = "date -s " + date;
	ret = syscall((char*)command.c_str(), 10000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "set date error");
			return false;
	}
	
	return true;
}

#include <limits.h>
#include <unistd.h>

std::string Misc::getExecutablePath()
{
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count != -1) {
        std::string path(result, count);
        return getFilepath(path); // Return directory only
    }
    return "";
}

bool Misc::mountSDCard(const std::string& target_path)
{
#ifdef BUILD_FOR_SIMULATION
	// PC模拟模式: 只创建目录，不执行真实mount
	Logger::log(LogLevel::INFO, "[SIM] mountSDCard: creating directory %s (no real mount)", target_path.c_str());
	if (!createDirectory(target_path)) {
		return false;
	}
	return true;
#else
	int ret;

	if (!createDirectory(target_path)) {
		return false;
	}

	std::string command = "mount /dev/mmcblk0p1 " + target_path;
	ret = syscall((char*)command.c_str(), 5000);
	if(ret < 0) {
			Logger::log(LogLevel::ERROR, "mount sdcard to %s error", target_path.c_str());
			return false;
	}
	
	return true;
#endif
}

void Misc::poweroff()
{
#ifdef BUILD_FOR_SIMULATION
	Logger::log(LogLevel::INFO, "[SIM] poweroff requested (not executed on PC)");
	return;
#else
	std::string command = "poweroff";
	syscall((char*)command.c_str(), 10000);
#endif
}

void Misc::reboot()
{
#ifdef BUILD_FOR_SIMULATION
	Logger::log(LogLevel::INFO, "[SIM] reboot requested (not executed on PC)");
	return;
#else
	std::string command = "reboot";
	syscall((char*)command.c_str(), 10000);
#endif
}
