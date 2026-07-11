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
    // 仅直接子目录名（DT_DIR，排除 "."/".."）。与 listFilenames(DT_REG) 互补。
    static std::vector<std::string> listSubdirectories(const std::string& dirname);
    // 递归删除目录树（native opendir/unlink/rmdir，禁 system/popen；OOM-safe）。
    static bool removeDirectory(const std::string& path);
    // 递归移动目录树（跨 fs 安全）：先 rename(2)；返 EXDEV（跨 fs）则递归 copy + delete 源。
    // 禁 system/popen（OOM-safe，与 copyFile/moveFile 的 fork-exec 路径不同——wm shutdown
    // 落卡用）。目标父目录须存在；碰撞由调用方预解析（本函数不覆盖）。
    // 成功后 src 不存在。ENOSPC/写失败返回 false（dst 可能残留半成品，调用方负责清理）。
    static bool moveDirectoryRecursive(const std::string& src, const std::string& dst);

    //network
    static std::string getIPAddress(const std::string &interface_name);
    static std::string getGatewayAddress(const std::string &interface_name);
    static std::string getMACAddress(const std::string &interface_name);
    static std::string findUsableNetworkInterface(const std::string &preferred_name = "");
    static std::string getNetworkInterfaceName();
	static void setNetworkInterfaceName(std::string name);
    static bool connectWifi(const std::string &ssid, const std::string &password);
    static bool startDHCP(const std::string &ifname="");
    // Read-only POSIX probes (SIM-safe, idempotent, no side effects).
    static bool isWifiDriverLoaded();
    static bool isWifiConnected(const std::string &ifname="wlan0");
    static bool ntpSync(const std::string& ntp_server);
    // Synchronize via ntpd, then block until system time is valid
    // (year > YEAR_MIN) or MAX_WAIT_SECONDS elapses. Returns true if time
    // became valid, false on ntpSync failure OR timeout. RTC writeback is
    // left to the CALLER (keeps common_misc free of a common_time_rtc dep).
    static bool ntpSyncAndWait(const std::string& ntp_server);

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
    static bool moduleLoaded(const char *name);   /* read /proc/modules — no fork (OOM-safe) */
private:
    static std::string netifname;
    static bool syscall_inited;
    static std::mutex syscall_mutex;
    static bool already_insmod_mmc;
};

#endif // MISC_H
