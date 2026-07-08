#include <sys/stat.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/reboot.h>
#include <sys/time.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <dirent.h>
#include "json/json.h"
#include "Common.h"
#include "Misc.h"
#include "Logger.h"
#include "ElogInit.h"
#include "system_call.h"
#include "StringConvert.h"

std::string Misc::netifname;
std::mutex Misc::syscall_mutex;
bool Misc::syscall_inited = false;
bool Misc::already_insmod_mmc = false;

namespace {

bool parseIpv4(const std::string& ip, uint32_t* host_order_addr)
{
    struct in_addr addr;
    if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
        return false;
    }

    if (host_order_addr != nullptr) {
        *host_order_addr = ntohl(addr.s_addr);
    }
    return true;
}

bool isPrivateIpv4(const std::string& ip)
{
    uint32_t addr = 0;
    if (!parseIpv4(ip, &addr)) {
        return false;
    }

    const uint8_t a = static_cast<uint8_t>((addr >> 24) & 0xFF);
    const uint8_t b = static_cast<uint8_t>((addr >> 16) & 0xFF);

    return a == 10
        || (a == 172 && b >= 16 && b <= 31)
        || (a == 192 && b == 168);
}

bool isLinkLocalIpv4(const std::string& ip)
{
    uint32_t addr = 0;
    if (!parseIpv4(ip, &addr)) {
        return false;
    }

    const uint8_t a = static_cast<uint8_t>((addr >> 24) & 0xFF);
    const uint8_t b = static_cast<uint8_t>((addr >> 16) & 0xFF);
    return a == 169 && b == 254;
}

bool isBenchmarkIpv4(const std::string& ip)
{
    uint32_t addr = 0;
    if (!parseIpv4(ip, &addr)) {
        return false;
    }

    const uint8_t a = static_cast<uint8_t>((addr >> 24) & 0xFF);
    const uint8_t b = static_cast<uint8_t>((addr >> 16) & 0xFF);
    return a == 198 && (b == 18 || b == 19);
}

int scoreInterfaceIpv4(const std::string& ip)
{
    if (isPrivateIpv4(ip)) {
        return 3;
    }
    if (isLinkLocalIpv4(ip)) {
        return 2;
    }
    if (isBenchmarkIpv4(ip)) {
        return 0;
    }
    return 1;
}

} // namespace

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

bool Misc::moduleLoaded(const char* name)
{
	/* Read /proc/modules directly — no fork-exec shell (OOM-safe). True iff a
	 * line starts with <name> (equivalent to `grep -q '^<name>' /proc/modules`). */
	if (!name || !*name) return false;
	std::ifstream f("/proc/modules");
	std::string line;
	while (std::getline(f, line)) {
		if (line.find(name) == 0) return true;
	}
	return false;
}

bool Misc::isWifiDriverLoaded()
{
#if defined(WIFI_TYPE_CYW43012)
	return moduleLoaded("cywdhd");
#elif defined(WIFI_TYPE_RTL8189FS)
	return moduleLoaded("8189fs");
#else
	#error "Unknown WiFi type"
#endif
}

bool Misc::isWifiConnected(const std::string &ifname)
{
	// Read-only POSIX probe: a non-empty IPv4 address AND a non-empty default
	// gateway on the interface means the link is actually up (guards against
	// mistaking a link-local address for a connection). Pure POSIX (getifaddrs +
	// /proc/net/route via getIPAddress/getGatewayAddress), SIM-safe.
	return !getIPAddress(ifname).empty() && !getGatewayAddress(ifname).empty();
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
#ifdef BUILD_FOR_SIMULATION
    const char* sim_override = std::getenv("SIM_NETIF_NAME");
    if (sim_override != nullptr && sim_override[0] != '\0') {
        std::string override_name(sim_override);
        if (!getIPAddress(override_name).empty()) {
            return override_name;
        }
        Logger::log(LogLevel::WARNING,
                    "SIM_NETIF_NAME=%s has no usable IPv4 address, fallback to auto detection",
                    override_name.c_str());
    }
#endif

    std::string best_name;
    int best_score = -1;

    if (!preferred_name.empty()) {
        std::string preferred_ip = getIPAddress(preferred_name);
        if (!preferred_ip.empty()) {
            best_name = preferred_name;
            best_score = scoreInterfaceIpv4(preferred_ip);
            if (best_score >= 3) {
                return preferred_name;
            }
        }
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
        if (candidate.empty()) {
            continue;
        }

        std::string ip = getIPAddress(candidate);
        if (ip.empty()) {
            continue;
        }

        int candidate_score = scoreInterfaceIpv4(ip);
        bool prefer_candidate = candidate_score > best_score;
        if (!prefer_candidate && candidate_score == best_score) {
            prefer_candidate = !preferred_name.empty() && candidate == preferred_name;
        }

        if (prefer_candidate) {
            fallback = candidate;
            best_name = candidate;
            best_score = candidate_score;
        }
    }

    freeifaddrs(ifaddr);
    if (!best_name.empty()) {
        return best_name;
    }
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

	if (isWifiConnected()) {
		Logger::log(LogLevel::INFO, "WiFi already connected, skip connectWifi");
		return true;
	}

	if (!isWifiDriverLoaded()) {
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
		// Re-check after insmod to absorb the EEXIST race: the kernel may report
		// "File exists" while the module is genuinely loaded. Treat a real load
		// as success and only fail if the module is still absent.
		if (!isWifiDriverLoaded()) {
			Logger::log(LogLevel::ERROR, "WiFi driver failed to load (%s)", command.c_str());
			return false;
		}
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

	if (!getIPAddress(netif).empty()) {
		Logger::log(LogLevel::INFO, "%s already has IP, skip DHCP", netif.c_str());
		return true;
	}

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

bool Misc::ntpSyncAndWait(const std::string& ntp_server)
{
	if (!Misc::ntpSync(ntp_server)) {
		Logger::log(LogLevel::ERROR, "ntp sync error");
		return false;
	}

	// Wait until system time is synchronized (year > YEAR_MIN(2000))
	const int MAX_WAIT_SECONDS = 30; // Maximum wait time 30 seconds
	const int CHECK_INTERVAL = 2;    // Check every 2 seconds
	int wait_time = 0;
	struct tm* nowtime = nullptr;
	while (wait_time < MAX_WAIT_SECONDS) {
		time_t now = time(nullptr);
		nowtime = localtime(&now);

		// Check if year is greater than YEAR_MIN
		if (nowtime->tm_year + YEAR_OFFSET > YEAR_MIN) {
			Logger::log(LogLevel::INFO, "System time synchronized: %d-%02d-%02d %02d:%02d:%02d",
			           nowtime->tm_year + YEAR_OFFSET, nowtime->tm_mon + MONTH_OFFSET, nowtime->tm_mday,
			           nowtime->tm_hour, nowtime->tm_min, nowtime->tm_sec);
			break;
		}

		Logger::log(LogLevel::INFO, "Waiting for system time synchronization, current year: %d, waited %d seconds",
		           nowtime->tm_year + YEAR_OFFSET, wait_time);
		sleep(CHECK_INTERVAL);
		wait_time += CHECK_INTERVAL;
	}

	if (wait_time >= MAX_WAIT_SECONDS) {
		Logger::log(LogLevel::WARNING, "Timeout waiting for system time synchronization after %d seconds", MAX_WAIT_SECONDS);
		return false;
	}

	return true;
}

bool Misc::getDateTime()
{
	time_t now = time(nullptr);
	struct tm tmv;
	localtime_r(&now, &tmv);
	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
	Logger::log(LogLevel::INFO, "date: %s", buf);
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
	/* settimeofday(2) — no fork-exec shell. Expects "%Y-%m-%d %H:%M:%S". */
	struct tm tmv;
	memset(&tmv, 0, sizeof(tmv));
	if (!strptime(date.c_str(), "%Y-%m-%d %H:%M:%S", &tmv)) {
		Logger::log(LogLevel::ERROR, "setDateTime: parse failed for '%s'", date.c_str());
		return false;
	}
	time_t t = mktime(&tmv);
	if (t == (time_t)-1) return false;
	struct timeval tv;
	tv.tv_sec = t;
	tv.tv_usec = 0;
	return settimeofday(&tv, nullptr) == 0;
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

	// Check if SD card is already mounted (read /proc/mounts — no fork-exec, OOM-safe)
	bool alreadyMounted = false;
	{
		std::ifstream mf("/proc/mounts");
		std::string line;
		while (std::getline(mf, line)) {
			if (line.find("/dev/mmcblk0p1") != std::string::npos) {
				alreadyMounted = true;
				break;
			}
		}
	}
	if (alreadyMounted) {
		Logger::log(LogLevel::INFO, "SD card already mounted, skip");
		return true;
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
	// Drain the async log ring and do a final flush BEFORE the board freezes —
	// otherwise the shutdown/poweroff-scene logs still buffered in the async
	// ring (and in libc's stdio buffer) are lost. Idempotent: safe even if elog
	// was never initialized (e.g. htc_daemon_app calling poweroff). SIM/devtest
	// _exit(0) paths rely on the consumer's 100 ms periodic flush instead.
	elog_deinit_all();
	sync();
	::reboot(RB_POWER_OFF);   /* direct syscall — no fork-exec shell (OOM-safe) */
	_exit(0);               /* reboot(2) returns only on failure */
#endif
}

void Misc::reboot()
{
#ifdef BUILD_FOR_SIMULATION
	Logger::log(LogLevel::INFO, "[SIM] reboot requested (not executed on PC)");
	return;
#else
	sync();
	::reboot(RB_AUTOBOOT);    /* direct syscall — no fork-exec shell (OOM-safe) */
	_exit(0);
#endif
}
