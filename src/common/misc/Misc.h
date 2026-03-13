#ifndef MISC_H
#define MISC_H
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

class Misc {
public:
    //file
    static std::string getFilepath(const std::string &pathname);
    static std::string getFilename(const std::string &file_pathname);
	static std::string getFileType(const std::string &file_pathname);
	static size_t getFileSize(const std::string &file_pathname);
    static bool copyFile(const std::string &src_pathname, const std::string &dst_pathname);
    static bool moveFile(const std::string &src_pathname, const std::string &dst_pathname);
    static bool deleteFile(const std::string& filePath);
    static bool createDirectory(const std::string& path, mode_t mode=0777);
    static bool isJsonFile(const std::string& filePath);
    static std::vector<std::string> listFilenames(const std::string& dirname);

    //network
    static std::string getIPAddress(const std::string &interface_name);
    static std::string getGatewayAddress(const std::string &interface_name);
    static std::string getMACAddress(const std::string &interface_name);
    static std::string findUsableNetworkInterface(const std::string &preferred_name = "");
    static std::string getNetworkInterfaceName();
	static void setNetworkInterfaceName(std::string name);
    static bool connectWifi(const std::string &ssid, const std::string &password);
    static bool startDHCP(const std::string &ifname="");
    static bool ntpSync(const std::string& ntp_server);

    //misc
    static bool setDateTime(const std::string& date);
    static bool getDateTime();
    static bool insmod_mmc();
    static bool mountSDCard(const std::string& target_path);
    static std::string getExecutablePath();
    static void poweroff();
    static void reboot();
    static int syscall(const char *command, int timeout_ms=1000);
    static int popencall(char *cmd, char *out, int max_size, int timeout_ms=1000);
private:
    static std::string netifname;
    static bool syscall_inited;
    static std::mutex syscall_mutex;
    static bool already_insmod_mmc;
    static bool already_inited_wifi;
};

#endif // MISC_H
