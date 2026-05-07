#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <cstring>
#include <limits.h>
#include <sys/stat.h>
#include <iomanip>
#include <sys/time.h>
#include <signal.h>
#include <json/json.h>
#include <csignal>
#include <queue>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <algorithm>


#include "MgmtServClient.h"
#include "RtspServer.h"
#include "http_server.h"
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "ElogInit.h"
#include "ImageSnap.h"
#include "VideoRecorder.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "utils/crc/CRC.h"
#include "Settings.h"
#include "MCU.h"
#include "Disk.h"
#include "AudioRecorder.h"
#include "AudioParams.h"
#include "StringConvert.h"
#include "WorkMode.h"
#include "app.h"
#include "DayNightSwitch.h"
#include "Power.h"
#include "utils/AutoRelease.h"
#include "time/rtc/RTC.h"
#include "daemon_api.h"
#include "DatabaseManager.h"
#include "MediaScanner.h"
#include "MdnsService.h"
#include "TcpEventService.h"
#include "Timezone.h"
#include "UsbDongle.h"
#include "CameraFactoryConfigImporter.h"

using namespace network;

using namespace media;
static std::shared_ptr<DayNightSwitch> daynight_switch;
std::shared_ptr<GPIO> gpio_rgb_led;

static std::string getCurrentTimeFormatted()
{
    time_t now = time(nullptr);
    struct tm* nowtime = localtime(&now);
    
    std::stringstream ss;
    ss << std::put_time(nowtime, "%Y%m%d_%H%M%S");
    Misc::getDateTime();
    return ss.str();
}

static bool getFileCreationTime(const std::string& filename, std::string& time_str)
{
    struct stat attr;
    if (stat(filename.c_str(), &attr) == 0) {
        time_str = Timezone::getFormattedTimeWithTimezone(attr.st_ctime);
        if (time_str.empty()) {
            Logger::log(LogLevel::ERROR, "Failed to get file creation time");
            return false;
        }
        char year[5] = {0}, mon[3] = {0}, day[3] = {0}, hour[3] = {0}, min[3] = {0}, sec[3] = {0};
        auto filename_no_path = filename.substr(filename.find_last_of('/') + 1);
        memset( year, 0, 5 );
        strncpy(year, filename_no_path.c_str(), 4);
        memset( mon, 0, 3 );
        strncpy(mon, filename_no_path.c_str() + 4, 2);
        memset( day, 0, 3 );
        strncpy(day, filename_no_path.c_str() + 6, 2);
        memset( hour, 0, 3 );
        strncpy(hour, filename_no_path.c_str() + 9, 2);
        memset( min, 0, 3 );
        strncpy(min, filename_no_path.c_str() + 11, 2);
        memset( sec, 0, 3 );
        strncpy(sec, filename_no_path.c_str() + 13, 2);
        
        // Create a time_t object from parsed components
        struct tm tm_info = {0};
        tm_info.tm_year = atoi(year) - YEAR_OFFSET;  // Years since 1900
        tm_info.tm_mon = atoi(mon) - MONTH_OFFSET;       // Months (0-11)
        tm_info.tm_mday = atoi(day);          // Day of month
        tm_info.tm_hour = atoi(hour);         // Hour
        tm_info.tm_min = atoi(min);           // Minute
        tm_info.tm_sec = atoi(sec);           // Second
        
        // Convert to time_t and use the function with timezone
        time_t parsed_time = mktime(&tm_info);
        time_str = Timezone::getFormattedTimeWithTimezone(parsed_time);
    }

    return true;
}

static std::string trimConfigString(const std::string& value)
{
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    std::string trimmed = value.substr(start, end - start);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

static void setEnvIfEmpty(const std::shared_ptr<EnvManager>& env_manager,
                          const std::string& key,
                          const std::string& value)
{
    if (env_manager->getEnv(key, "").empty()) {
        env_manager->setEnv(key, value);
    }
}

static std::string normalizePath(const std::string& path)
{
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) {
        return std::string(resolved);
    }
    return path;
}

static std::string getParentPath(const std::string& path)
{
    if (path.empty()) {
        return "";
    }

    std::string trimmed = path;
    while (trimmed.size() > 1 && trimmed.back() == '/') {
        trimmed.pop_back();
    }

    const size_t pos = trimmed.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    if (pos == 0) {
        return "/";
    }
    return trimmed.substr(0, pos);
}

static MediaScannerMode parseMediaScannerMode(const std::string& rawMode)
{
    std::string mode = trimConfigString(rawMode);
    std::transform(mode.begin(), mode.end(), mode.begin(), ::tolower);
    if (mode == "full" || mode == "full_scan" || mode == "media") {
        return MediaScannerMode::FullScan;
    }
    return MediaScannerMode::PendingThumbnails;
}

static uint16_t getConfiguredPort(const std::shared_ptr<DeviceConfig>& config,
                                  const std::string& section,
                                  const std::string& key,
                                  uint16_t default_port)
{
    int configured_port = config->get(section, key, static_cast<int>(default_port));
    if (configured_port <= 0 || configured_port > 65535) {
        return default_port;
    }
    return static_cast<uint16_t>(configured_port);
}

static std::string getDefaultMdnsInstanceName(const std::shared_ptr<DeviceConfig>& config)
{
    std::string instance_name = trimConfigString(config->get(INI_SECTION_MDNS, INI_KEY_MDNS_INSTANCE_NAME, ""));
    if (!instance_name.empty()) {
        return instance_name;
    }

    instance_name = trimConfigString(config->get(INI_SECTION_BOOT, INI_KEY_PNAME, ""));
    if (!instance_name.empty()) {
        return instance_name;
    }

    instance_name = trimConfigString(config->get(INI_SECTION_DEVICE, INI_KEY_PID, ""));
    if (!instance_name.empty()) {
        return instance_name;
    }

    return "T32Camera";
}

static std::string getDefaultMdnsHostName(const std::shared_ptr<DeviceConfig>& config)
{
    std::string host_name = trimConfigString(config->get(INI_SECTION_MDNS, INI_KEY_MDNS_HOST_NAME, ""));
    if (!host_name.empty()) {
        return host_name;
    }

    host_name = trimConfigString(config->get(INI_SECTION_DEVICE, INI_KEY_PID, ""));
    if (!host_name.empty()) {
        return host_name;
    }

    return "t32cam";
}

static service::MdnsServiceParams buildMdnsParams(const std::shared_ptr<DeviceConfig>& config,
                                                  const std::string& interface_name,
                                                  const std::string& ip_address,
                                                  uint16_t ctrl_port,
                                                  uint16_t rtsp_port)
{
    service::MdnsServiceParams params;
    params.interfaceName = interface_name;
    params.ipAddress = ip_address;
    params.serviceType = trimConfigString(
        config->get(INI_SECTION_MDNS, INI_KEY_MDNS_SERVICE_TYPE, "_t32cam._tcp"));
    params.instanceName = getDefaultMdnsInstanceName(config);
    params.hostName = getDefaultMdnsHostName(config);
    params.txt.deviceFamily = service::kDefaultMdnsDeviceFamily;
    params.txt.model = trimConfigString(config->get(INI_SECTION_BOOT, INI_KEY_PMODEL, "T32"));
    params.txt.serialNumber = trimConfigString(config->get(INI_SECTION_DEVICE, INI_KEY_PID, ""));
    params.txt.firmwareVersion = CAMERA_VERSION;
    params.txt.rtspPort = rtsp_port;
    params.txt.ctrlPort = ctrl_port;
    params.txt.macAddress = Misc::getMACAddress(interface_name);
    params.txt.status = "ready";
    return params;
}

static bool isMdnsEnabled(const std::shared_ptr<DeviceConfig>& config)
{
    return config->get(INI_SECTION_MDNS, INI_KEY_MDNS_ENABLE, 1) != 0;
}

static int generateDescInfo(std::vector<std::string>& files, std::string& desc_info)
{
    auto settings = Settings::getInstance();
    char temp_buf[32] = {0};
    auto mcu = MCU::getInstance();
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    
    // Use the reusable function to format current time with dynamic timezone
    std::string current_time_str = Timezone::getFormattedTimeWithTimezone(tv.tv_sec);
    if (current_time_str.empty()) {
        Logger::log(LogLevel::ERROR, "Failed to get current time string");
        return -1;
    }

    Json::Value json_root;
    json_root["F_UploadedTag"] = 0;
    Json::Value file_inf_array(Json::arrayValue);

    for ( auto& filename : files ) {
        if (filename.empty()) { 
            Logger::log(LogLevel::INFO, "empty file name");
            continue;
        }
        std::string file_creation_time;    
        if (!getFileCreationTime(filename, file_creation_time)) {
            Logger::log(LogLevel::INFO, "Failed to get file creation time for %s", filename.c_str());
            continue;
        }

        Json::Value file_item;
        file_item["F_FilePath"] = Misc::getFilepath(filename);
        file_item["F_FileName"] = Misc::getFilename(filename);
        file_item["F_FileTime"] = file_creation_time;
        file_item["F_UploadedTag"] = 0;

        uint16_t check_code = 0x0000;
        if (CRC::calculate_crc16(filename, check_code)) {
            file_item["F_CheckCode"] = static_cast<int>(check_code);
        }

        file_inf_array.append(file_item);
    }
    json_root["file_inf"] = file_inf_array;

    //device
    Json::Value device_obj;
    {
        device_obj["PID"] = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
        device_obj["EUID"] = "";
        device_obj["IP"] = Misc::getIPAddress(Misc::getNetworkInterfaceName());
        device_obj["GP"] = mcu->readGps();

        auto lowpower_volte = mcu->readLowPowerVoltage();
        auto battery1_volte = mcu->readBattery1Voltage();
        auto battery2_volte = mcu->readBattery2Voltage();
        device_obj["Battery1"] = mcu->convertVoltage(battery1_volte);
        device_obj["Battery2"] = mcu->convertVoltage(battery2_volte);

        auto ext_volte = mcu->readExternalVoltage();
        auto shutdown_volte = mcu->readShutdownVoltage();
        if ( ext_volte <= 14 || battery1_volte <= shutdown_volte ) {
            device_obj["SPower"] = "0";
            device_obj["EPower"] = mcu->convertVoltage(ext_volte);
        } else {
            device_obj["EPower"] = "0";
            device_obj["SPower"] = mcu->convertVoltage(ext_volte);
        }

        auto disk_info = Disk::getInfo(DISK_PATHNAME);
        int used = (disk_info.total - disk_info.free) * 10 / 1024;
        int total = disk_info.total * 10 / 1024;
        
        snprintf(temp_buf, sizeof(temp_buf), "%d.%d/%d.%d G", used / 10, used % 10, total / 10, total % 10 );
        device_obj["Memory"] = temp_buf;
        device_obj["WMode"] = 0;
        device_obj["ONTime"] = settings->onTime_0 + (settings->onTime_1 << 8);

        device_obj["NStatus"] = 0;

        if ( 1 ) {
            device_obj["AStatus"] = 22;
        }
        else if ( battery1_volte <= shutdown_volte && ext_volte <= shutdown_volte ) {
            device_obj["AStatus"] = 23;  /*powroff*/
        }
        else if ( battery1_volte <= lowpower_volte && ext_volte <= lowpower_volte  ) {
            device_obj["AStatus"] = 21;  /*low*/
        }
        else {
            device_obj["AStatus"] = 11;
        }

        auto battery1_level = mcu->readBatteryLevel();
        device_obj["BAT1_Level"] = battery1_level;

        device_obj["Low_PWR_Val"] = mcu->convertVoltage(lowpower_volte);
        device_obj["Loff_PWR_Val"] = mcu->convertVoltage(shutdown_volte);

        device_obj["UTime"] = current_time_str;

    }
    json_root["device"] = device_obj;

    //data
    Json::Value data_obj;
    {
        #if USER_CONFIG_WPWS
        data_obj["D_Type"] = mcu->readEventType();
        data_obj["D_Id"] = mcu->readEventID();
        data_obj["D_Num"] = mcu->readEventNum();
        #else
        data_obj["D_Temperature"] = to_string_custom(mcu->readTemperature());
        data_obj["D_Humidity"] = to_string_custom(mcu->readHumidity());
        data_obj["D_Atmos"] = to_string_custom(mcu->readAtmosPressure());

        snprintf(temp_buf, sizeof(temp_buf), "%08X", mcu->readRMID());
        data_obj["D_SensorPID"] = temp_buf;
        data_obj["D_SensorType"] = mcu->readRMType();
        data_obj["D_SensorValue"] = mcu->readRMValue();
        auto rm_bat_v = mcu->readBatteryVoltage();
        auto rm_bat1_v = mcu->readBattery1Voltage();
        auto rm_bat2_v = mcu->readBattery2Voltage();
        data_obj["D_SensorBattery"] = mcu->convertVoltage(rm_bat_v);
        data_obj["D_SensorBattery1"] = mcu->convertVoltage(rm_bat1_v);
        data_obj["D_SensorBattery2"] = mcu->convertVoltage(rm_bat2_v);
        data_obj["D_SensorGP"] = "";
        data_obj["D_SensorCount"] = mcu->readRMCount();
        auto rm_sp_v = mcu->readRMSunPowerValue();
        data_obj["D_SensorSP"] = mcu->convertVoltage(rm_sp_v);
        #endif
    }
    json_root["data"] = data_obj;

    //network
    Json::Value network_obj;
    {
        network_obj["N_UPID"] = DeviceConfig::getInstance()->get(INI_SECTION_SYS, INI_KEY_UPID, "CKVISON");
        network_obj["N_UIP"] = "0";
        network_obj["N_CStatus"] = 0;
        network_obj["N_CIP"] = "0";
        network_obj["N_MStatus"] = 0;
        network_obj["N_MIP"] = "0";
    }
    json_root["network"] = network_obj;

    //signal
    Json::Value signal_obj;
    {
        auto IsWifiStationReady = mcu->IsWifiStationReady();
        if (IsWifiStationReady) {
            signal_obj["S_CF"] = mcu->readSignalCF();
            signal_obj["S_RSSI"] = mcu->readSignalRSSI();
            signal_obj["S_RL"] = 0;
            signal_obj["S_RSRP"] = mcu->readSignalRSRP();
            signal_obj["S_RSRQ"] = mcu->readSignalRSRQ();
            signal_obj["S_SNR"] = mcu->readSignalSNR();
            signal_obj["S_TD"] = mcu->readSignalTD();
            signal_obj["S_TP"] = mcu->readSignalTP();
        } else {
            auto program_type = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0);
            if (PTYPE_USB_DONGLE == program_type && mcu->Is4gExist()) {
                signal_obj["S_RSSI"] = mcu->readSignalRSSI();
                signal_obj["S_CF"] = mcu->readSignalCF();
            } else {
                signal_obj["S_RSSI"] = 0;
                signal_obj["S_CF"] = 0;
            }

            signal_obj["S_RL"] = 0;
            signal_obj["S_RSRP"] = 0;
            signal_obj["S_RSRQ"] = 0;
            signal_obj["S_SNR"] = 0;
            signal_obj["S_TD"] = 0;
            signal_obj["S_TP"] = 0;
        }
    }
    json_root["signal"] = signal_obj;

    Json::StreamWriterBuilder writer_builder;
    desc_info = Json::writeString(writer_builder, json_root);

    return EC_SUCCESS;
}

static int createDescInfoFile(std::vector<std::string>& media_files, const std::string &desc_filename)
{
    std::string desc_info;
    int ret = generateDescInfo(media_files, desc_info);
    if (ret != EC_SUCCESS) {
        return ret;
    }

    auto fp = fopen(desc_filename.c_str(), "w+");
    if (!fp) {
        return EC_OPEN_FILE_FAILED;
    }

    fwrite(desc_info.c_str(), 1, desc_info.length(), fp);

    fclose(fp);

    return EC_SUCCESS;
}

static bool syncWithMCU()
{
    auto devconf = DeviceConfig::getInstance();
    auto mcu = MCU::getInstance();
    //PID
    {
        auto pid = mcu->readPID();
        if (!pid.empty()) {
            devconf->set(INI_SECTION_DEVICE, INI_KEY_PID, pid);
        }
    }

    //UPID & UPWD
    {
        auto upid = mcu->readUPID();
        auto upwd = mcu->readUPWD();
        if (!upid.empty() && !upwd.empty()) {
            devconf->set(INI_SECTION_SYS, INI_KEY_UPID, upid);
            devconf->set(INI_SECTION_SYS, INI_KEY_UPWD, upwd);
        }
    }
    devconf->flush();

    //RTC
    {
        time_t now = time(nullptr);
        struct tm* datetime = localtime(&now);
        if (datetime != nullptr) {
            mcu->setDatetime(datetime);
        }
    }
    return true;
}

// 简单的 INI 配置解析器
static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> parseIniFile(const std::string& filename)
{
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> config;
    std::ifstream file(filename);
    if (!file.is_open()) {
        return config;
    }
    
    std::string currentSection;
    std::string line;
    while (std::getline(file, line)) {
        // 去除首尾空白
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        // 跳过空行和注释
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        // 解析 section
        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }
        
        // 解析 key=value
        size_t eqPos = line.find('=');
        if (eqPos != std::string::npos) {
            std::string key = line.substr(0, eqPos);
            std::string value = line.substr(eqPos + 1);
            
            // 去除 key 和 value 的首尾空白
            start = key.find_first_not_of(" \t");
            end = key.find_last_not_of(" \t");
            if (start != std::string::npos) key = key.substr(start, end - start + 1);
            
            start = value.find_first_not_of(" \t");
            end = value.find_last_not_of(" \t");
            if (start != std::string::npos) value = value.substr(start, end - start + 1);
            
            config[currentSection][key] = value;
        }
    }
    return config;
}

static bool processCmdSnap(bool is_rtc_work_well) {
    //move media file from /tmp to sdcard
    std::vector<std::string> file_names;
    
    std::ifstream jsonFile(QUICK_SNAP_INFO_FILE);
    if (jsonFile.is_open()) {
        Json::Value root;
        Json::CharReaderBuilder readerBuilder;
        std::string errs;
        if (!Json::parseFromStream(readerBuilder, jsonFile, &root, &errs)) {
            Logger::log(LogLevel::ERROR, "Parse json file failed");
            return false;
        } 
        auto dir = root["dir"].asString();
        auto files = root["files"];
        std::string oldpath = QUICK_SNAP_DIR + dir + "/*";
        std::string newpath, upload_path, timeStr;
        if (is_rtc_work_well) {
            #if ALL_MEDIA_FILE_IN_ONE_FOLDER
            newpath =  MEDIA_STORE_FOLDER_PATH;
            #else
            newpath =  MEDIA_TARGET_PATH + dir;
            #endif
            upload_path = MEDIA_UPLOAD_PATH + dir;
        } else {
            timeStr = getCurrentTimeFormatted();
            #if ALL_MEDIA_FILE_IN_ONE_FOLDER
            newpath =  MEDIA_STORE_FOLDER_PATH;
            #else
            newpath =  MEDIA_TARGET_PATH + timeStr;
            #endif
            upload_path = MEDIA_UPLOAD_PATH + timeStr;
        }

        if (!Misc::createDirectory(newpath) || !Misc::createDirectory(MEDIA_UPLOAD_PATH) || !Misc::moveFile(oldpath, newpath)) {
            Logger::log(LogLevel::ERROR, "move %s to %s failed", oldpath.c_str(), newpath.c_str());
            return false;
        }

        //create desc file
        for (auto & file : files) {
            std::string filename; 
            if (is_rtc_work_well) {
                filename = newpath + "/" + file.asString();
            } else {
                auto oldname = newpath + "/" + file.asString();
                filename = newpath + "/" + timeStr + "_" + file.asString();
                Logger::log(LogLevel::INFO, "rename %s to %s", oldname.c_str(), filename.c_str());
                Misc::moveFile(oldname, filename);
            }
            file_names.push_back(filename);
        }

        auto desc_filename = upload_path + ".json";
        createDescInfoFile(file_names, desc_filename);
    }
    
    if (file_names.empty()) {//only for test
        file_names = {
            "./res/20250620_101358.JPG", 
            "./res/20250620_101458.JPG", 
            "./res/20250620_101558.JPG"
        };
        auto snap_param = ImageSnapParams();
        auto imageSnap = std::make_shared<ImageSnap>(snap_param);
        imageSnap->snap(file_names);
        createDescInfoFile(file_names, "./res/20250620_101358.json");
    }
    return true;
}
static void printUsage(char *argv[])
{
    std::cout << "Usage: " << argv[0] << " <command> [options]" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  -w, --wifi\t\tConnect to a Wi-Fi network" << std::endl;
    std::cout << "  -d, --dhcp\t\tGet IP address from DHCP server" << std::endl;
    std::cout << "  -n, --ntp\t\tSync time with NTP server" << std::endl;
    std::cout << "  -h, --help\t\tDisplay this help message" << std::endl;
    std::cout << "  -a, --auth\t\tAuthenticate with the management server" << std::endl;
    std::cout << "  -hb, --heartbeat\tSend a heartbeat message to the management server" << std::endl;
    std::cout << "  -s, --snap\t\tSnap an image" << std::endl;
    std::cout << "  -u, --upload\t\tUpload a file to the storage server" << std::endl;
    std::cout << "  -ar, --audio-record\tRecord audio" << std::endl;
    std::cout << "  -vr, --video-record\tRecord video" << std::endl;
    std::cout << "  -m, --mobile\t\tConnect to the mobile network" << std::endl;
    std::cout << "  -rs, --rtsp-server\tStart the RTSP server (use --no-audio to disable audio)" << std::endl;
    std::cout << "  -grtc, --get-rtc\tGet RTC time" << std::endl;
    std::cout << "  -srtc, --set-rtc\tSet RTC time" << std::endl;
    std::cout << "  -uv, --uvc\t\tStart the UVC" << std::endl;
}

#define CMD_HELP 0
#define CMD_CONN_NET (1 << 0)
#define CMD_DHCP (1 << 1)
#define CMD_SNAP (1 << 2)
#define CMD_AUDIO_RECORD (1 << 3)
#define CMD_VIDEO_RECORD (1 << 4)
#define CMD_AUTH (1 << 5)
#define CMD_HEARTBEAT (1 << 6)
#define CMD_UPLOAD (1 << 7)
#define CMD_MOBILE (1 << 8)
#define CMD_RTSP_SERVER (1 << 9)
#define CMD_NTP (1 << 10)
#define CMD_GET_RTC (1 << 11)
#define CMD_SET_RTC (1 << 12)

static bool already_in_exit_flow = false;
static bool rtsp_audio_enabled = true;  // RTSP 音频默认开启
static std::shared_ptr<MgmtServClient> mgmtServClient = nullptr;
static std::shared_ptr<StorageServClient> storageServClient = nullptr;
// Signal handler for CTRL+C
// 信号处理消息结构体
enum class SignalMessageType {
    EXIT,        // 退出信号(SIGINT)
    SHUTDOWN,    // 关闭信号(SIGTERM)
    STOP_THREAD  // 停止工作线程
};

struct SignalMessage {
    SignalMessageType type;
    // 可以根据需要添加更多字段
};

struct SignalResult {
    bool success;
    // 可以根据需要添加更多字段
};

// 全局变量用于线程间通信
std::queue<SignalMessage> signalMessageQueue;
std::mutex messageMutex;
std::condition_variable messageCondition;

SignalResult signalResult;
std::mutex resultMutex;
std::condition_variable resultCondition;
bool resultReady = false;

// 工作线程函数，执行实际的信号处理逻辑
static void signalHandlerThreadFunc() {
    while (true) {
        SignalMessage message;
        
        // 等待消息
        {            
            std::unique_lock<std::mutex> lock(messageMutex);
            messageCondition.wait(lock, []{ return !signalMessageQueue.empty(); });
            
            message = signalMessageQueue.front();
            signalMessageQueue.pop();
        }
        
        // 处理消息
        SignalResult result = {true};
        
        switch (message.type) {
            case SignalMessageType::EXIT: {
                Logger::log(LogLevel::INFO, "Processing SIGINT in worker thread...");
                if (daynight_switch) {
                    daynight_switch->controlISP(DayNightState::DAY);
                    daynight_switch->controlIRLed(DayNightState::DAY);
                    daynight_switch->controlIRCut(DayNightState::DAY);
                }

                if (gpio_rgb_led) {
                    gpio_rgb_led->setConstant(GPIO_VALUE::LOW);
                }

                std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", ""); 
                if (setting_file_path.empty()) {
                    Logger::log(LogLevel::ERROR, "Failed to get setting file path");
                    result.success = false;
                } else {
                    if (!Settings::getInstance()->saveToJsonFile(setting_file_path)) {
                        Logger::log(LogLevel::ERROR, "Failed to save setting file: %s", setting_file_path.c_str());
                        result.success = false;
                    }
                }

                if (http_server_is_running()) {
                    http_server_stop();
                    http_server_deinit();
                }
                service::TcpEventService::getInstance()->stop();
                mgmtServClient = nullptr;
                storageServClient = nullptr;

                break;
            }
            
            case SignalMessageType::SHUTDOWN: {
                Logger::log(LogLevel::INFO, "Processing SIGTERM in worker thread...");
                
                if (daynight_switch) {
                    daynight_switch->controlISP(DayNightState::DAY);
                    daynight_switch->controlIRLed(DayNightState::DAY);
                    daynight_switch->controlIRCut(DayNightState::DAY);
                }

                if (gpio_rgb_led) {
                    gpio_rgb_led->setConstant(GPIO_VALUE::LOW);
                }

                std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", ""); 
                if (setting_file_path.empty()) {
                    Logger::log(LogLevel::ERROR, "Failed to get setting file path");
                    result.success = false;
                } else {
                    if (!Settings::getInstance()->saveToJsonFile(setting_file_path)) {
                        Logger::log(LogLevel::ERROR, "Failed to save setting file: %s", setting_file_path.c_str());
                        result.success = false;
                    }
                }

                if (http_server_is_running()) {
                    http_server_stop();
                    http_server_deinit();
                }
                service::TcpEventService::getInstance()->stop();
                mgmtServClient = nullptr;
                storageServClient = nullptr;
            
                if (Power::getInstance()->isChangeModeRequested()) {
                    Logger::log(LogLevel::INFO, "Change mode requested, not powering off");
                    auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
                    if (!gpio_power_hold.exportGPIO() || !gpio_power_hold.setDirection(GPIO_DIRECTION::OUTPUT)
                        || !gpio_power_hold.setValue(GPIO_VALUE::HIGH)) {
                        Logger::log(LogLevel::ERROR, "%s Failed to set power hold pin", __func__);
                        result.success = false;
                    }
                } else {
                    auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
                    if (!gpio_power_hold.exportGPIO() || !gpio_power_hold.setDirection(GPIO_DIRECTION::OUTPUT)
                        || !gpio_power_hold.setValue(GPIO_VALUE::LOW)) {
                        Logger::log(LogLevel::ERROR, "%s Failed to set power hold pin", __func__);
                        result.success = false;
                    }
                }
                break;
            }
            
            case SignalMessageType::STOP_THREAD: {
                Logger::log(LogLevel::INFO, "Stopping signal handler thread...");
                return; // 退出线程
            }
            
            default:
                Logger::log(LogLevel::WARNING, "Unknown signal message type");
                result.success = false;
                break;
        }
        
        // 发送结果回signalHandler
        {            
            std::lock_guard<std::mutex> lock(resultMutex);
            signalResult = result;
            resultReady = true;
        }
        resultCondition.notify_one();
    }
}

static void signalHandler(int signal)
{
    if (already_in_exit_flow) {
        return;
    }
    SignalMessage message;
    if (signal == SIGINT) {
        Logger::log(LogLevel::INFO, "Received SIGINT, sending to worker thread...");
        message.type = SignalMessageType::EXIT;
    }
    else if (signal == SIGTERM) {
        Logger::log(LogLevel::INFO, "Received SIGTERM, sending to worker thread...");
        message.type = SignalMessageType::SHUTDOWN;
    }
    else {
        Logger::log(LogLevel::WARNING, "Received unhandled signal: %d", signal);
        return;
    }
    
    // 将消息放入队列
    {        
        std::lock_guard<std::mutex> lock(messageMutex);
        signalMessageQueue.push(message);
    }
    messageCondition.notify_one();
    
    // 等待工作线程完成处理并返回结果，最多等待2秒
    {        
        std::unique_lock<std::mutex> lock(resultMutex);
        auto waitResult = resultCondition.wait_for(lock, std::chrono::seconds(2), []{ return resultReady; });
        
        if (!waitResult) {
            Logger::log(LogLevel::WARNING, "Signal processing timed out after 2 seconds");
        } else {
            if (signalResult.success) {
                Logger::log(LogLevel::INFO, "Signal processing completed successfully");
            } else {
                Logger::log(LogLevel::ERROR, "Signal processing completed with errors");
            }
            
            resultReady = false;
        }
    }
    already_in_exit_flow = true;
#ifdef BUILD_FOR_SIMULATION
    // PC 模拟模式下，SIGTERM 不执行 poweroff/reboot，让程序正常退出
    if (signal == SIGTERM) {
        Logger::log(LogLevel::INFO, "[SIM] SIGTERM received, program will exit normally");
    }
#else
    if (signal == SIGTERM) {
        if (Power::getInstance()->isChangeModeRequested()) {
            Logger::log(LogLevel::INFO, "Waiting 2 seconds before reboot...");
            DeviceConfig::getInstance()->flush();
            sleep(2);
            Misc::reboot();
            while(1);
        } else {
            Logger::log(LogLevel::INFO, "Waiting 2 seconds before power off...");
            DeviceConfig::getInstance()->flush();
            sleep(2);
            Misc::poweroff();
            while(1);
        }
    }
#endif
}

// 工作线程全局变量
std::thread signalHandlerThread;

int main(int argc, char* argv[])
{
    std::string timezone;
    bool update_config_exists = false;
    bool is_rtc_work_well = true;
    enum workingMode working_mode = workingMode::WORKING_MODE_MAX;
#ifdef BUILD_FOR_SIMULATION
    // 动态计算路径，确保文件生成在 build 目录下
    std::string exePath = Misc::getExecutablePath();
    std::string projectRootPath = normalizePath(exePath + "/../..");  // build_sim/bin/../.. -> project_root
    std::string defaultSimRootPath = normalizePath(projectRootPath + "/sim_sdcard_runtime");
    const char* envSimRoot = std::getenv("SIM_SD_ROOT");
    std::string simRootPath = (envSimRoot && envSimRoot[0] != '\0')
                                  ? normalizePath(envSimRoot)
                                  : defaultSimRootPath;
    setenv("SIM_SD_ROOT", simRootPath.c_str(), 0);

    auto env_manager = EnvManager::getInstance();
    setEnvIfEmpty(env_manager, "CONFIG_FILE", projectRootPath + "/res/config.sim.ini");
    setEnvIfEmpty(env_manager, "SETTING_FILE_PATH", projectRootPath + "/res/setting.json");
    setEnvIfEmpty(env_manager, "BROADCAST_FILELIST_PATHNAME", simRootPath + "/media/audio/AUDIO_PLAY_LIST.txt");
    setEnvIfEmpty(env_manager, "BROADCAST_FILE_PATH", simRootPath + "/media/audio/");
    setEnvIfEmpty(env_manager, "ISP_FILE_PATH", simRootPath + "/media/audio/");
#else
    EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME);//必须放在main函数的最开始位置
#endif

    // Initialize Database
#ifdef BUILD_FOR_SIMULATION
    std::string db_path = simRootPath + "/data/db";
    std::string media_root = simRootPath + "/DCIM";
    const char* envLogDir = std::getenv("SIM_LOG_DIR");
    std::string log_root = (envLogDir && envLogDir[0] != '\0')
                           ? std::string(envLogDir)
                           : (simRootPath + "/logs");
    std::string log_file = log_root + "/app.log";
#else
    std::string db_path = EnvManager::getInstance()->getEnv("DB_PATH", "/sdcard/data/db");
    std::string media_root = "/sdcard/DCIM";
    std::string log_root = "/sdcard/logs";
    std::string log_file = log_root + "/app.log";
#endif

    if (!DatabaseManager::getInstance().init(db_path)) {
        fprintf(stderr, "Failed to initialize Database\n");
    }

    // Start Media Scanner (async). Default mode uses the small pending-thumbnail
    // directory as a recovery queue; full scan is reserved for maintenance.
    struct stat st;
    if (stat(media_root.c_str(), &st) == 0) {
        MediaScannerOptions scannerOptions;
        scannerOptions.mediaRootDir = media_root;
        scannerOptions.mode = parseMediaScannerMode(
            EnvManager::getInstance()->getEnv("MEDIA_SCANNER_MODE", "pending_thumb"));

        const std::string dataRoot = getParentPath(db_path);
        const std::string defaultPendingThumbDir =
            dataRoot.empty() ? (db_path + "/thumb_pending") : (dataRoot + "/thumb_pending");
        scannerOptions.pendingThumbDir =
            EnvManager::getInstance()->getEnv("THUMB_PENDING_DIR", defaultPendingThumbDir);

        MediaScanner::getInstance().startScan(scannerOptions);
    }
    
    // Initialize EasyLogger - must be called early before any logging
#ifdef BUILD_FOR_SIMULATION

    // Ensure log directory exists
    Misc::createDirectory(log_root);

    // PC 模拟环境：启用终端和文件日志，默认保存到 sim_sdcard_runtime/logs/
    ElogConfig elog_config;
    elog_config.enableTerminal = true;
    elog_config.enableFile = true;
    elog_config.logFilePath = log_file;
    elog_config.logLevel = ELOG_LVL_DEBUG;
    if (!elog_init_with_config(elog_config)) {
        fprintf(stderr, "Failed to initialize EasyLogger\n");
    }
    
    Logger::log(LogLevel::INFO, "[SIM] Simulation Root: %s", simRootPath.c_str());
    Logger::log(LogLevel::INFO, "[SIM] Project Root: %s", projectRootPath.c_str());
    Logger::log(LogLevel::INFO, "[SIM] Log Directory: %s", log_root.c_str());
    Logger::log(LogLevel::INFO, "[SIM] Log File: %s", log_file.c_str());
#else
    // 真机环境：终端 + 文件输出
    Misc::createDirectory(log_root);
    ElogConfig elog_config;
    elog_config.enableTerminal = true;
    elog_config.enableFile = true;
    elog_config.logFilePath = log_file;
    elog_config.logLevel = ELOG_LVL_INFO;
    if (!elog_init_with_config(elog_config)) {
        fprintf(stderr, "Failed to initialize EasyLogger\n");
    }
    Logger::log(LogLevel::INFO, "Log File: %s", log_file.c_str());
#endif
    
    daynight_switch = DayNightSwitch::getInstance();
    if (daynight_switch) {
        daynight_switch->setCdsPins(CDS_SENSOR_PIN);
        daynight_switch->setIRLedPins(IR_LED_PIN);
        daynight_switch->setIRCutPins(IR_CUT_ENABLE_PIN, IR_CUT_CTRL_PIN);
    }

    gpio_rgb_led = std::make_shared<GPIO>(RGB_LED_PIN);
    if (!gpio_rgb_led->exportGPIO() || !gpio_rgb_led->setDirection(GPIO_DIRECTION::OUTPUT)) {
        Logger::log(LogLevel::ERROR, "export or set gpio(%d) direction output failed", RGB_LED_PIN);
        gpio_rgb_led = nullptr;
    }

    AutoRelease auto_release([]() {
        // Use the static variable directly without capturing
        if (daynight_switch) {
            daynight_switch->controlISP(DayNightState::DAY);
            daynight_switch->controlIRCut(DayNightState::DAY);
            daynight_switch->controlIRLed(DayNightState::DAY);
        }

        if (gpio_rgb_led) {
            gpio_rgb_led->setConstant(GPIO_VALUE::LOW);
        }
    });
    
    // Register signal handler for CTRL+C
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    int command = CMD_HELP;
    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {  
        printUsage(argv);
        return -1;
    }

    // 启动信号处理工作线程
    signalHandlerThread = std::thread(signalHandlerThreadFunc);

    if (argc != 5) {
        if (std::string(argv[1]) == "-w" || std::string(argv[1]) == "--wifi") {
            command = CMD_CONN_NET;
        } else if (std::string(argv[1]) == "-d" || std::string(argv[1]) == "--dhcp") { 
            command = CMD_DHCP;
        } else if (std::string(argv[1]) == "-s" || std::string(argv[1]) == "--snap") {
            command = CMD_SNAP;
        } else if (std::string(argv[1]) == "-qs" || std::string(argv[1]) == "--quick-snap") {
            command = CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_SNAP | CMD_UPLOAD;
        } else if (std::string(argv[1]) == "-ar" || std::string(argv[1]) == "--audio-record") {
            command = CMD_AUDIO_RECORD;
        } else if (std::string(argv[1]) == "-vr" || std::string(argv[1]) == "--video-record") {
            command = CMD_VIDEO_RECORD;
        } else if (std::string(argv[1]) == "-a" || std::string(argv[1]) == "--auth") { 
            command = CMD_AUTH;
        } else if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--heartbeat") {
            command = CMD_HEARTBEAT;
        } else if (std::string(argv[1]) == "-u" || std::string(argv[1]) == "--upload") {
            command = CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD;
        } else if (std::string(argv[1]) == "-m" || std::string(argv[1]) == "--mobile") {
            command = CMD_MOBILE;
        } else if (std::string(argv[1]) == "-n" || std::string(argv[1]) == "--ntp") {
            command = CMD_CONN_NET | CMD_NTP;
        } else if (std::string(argv[1]) == "-rs" || std::string(argv[1]) == "--rtsp-server") {
            command = CMD_RTSP_SERVER;
            // 检查是否有 --no-audio 参数
            for (int i = 2; i < argc; i++) {
                if (std::string(argv[i]) == "--no-audio") {
                    rtsp_audio_enabled = false;
                }
            }
        } else if (std::string(argv[1]) == "-grtc" || std::string(argv[1]) == "--get-rtc") {
            command = CMD_GET_RTC;
        } else if (std::string(argv[1]) == "-srtc" || std::string(argv[1]) == "--set-rtc") {
            command = CMD_SET_RTC;
        } else {
            printUsage(argv);
            return -1;
        }
    } else {
        if ((std::string(argv[1]) == "-wm" || std::string(argv[1]) == "--work-mode") && (std::string(argv[3]) == "-rtc" || std::string(argv[3]) == "--rtc-status")) {
            working_mode = (enum workingMode)stoi_custom(argv[2]);
            is_rtc_work_well = (bool)stoi_custom(argv[4]);
            Logger::log(LogLevel::INFO, "%s working mode %d, rtc status %d", __func__, working_mode, is_rtc_work_well);
            switch (working_mode) {
                case WORKING_MODE_SNAP_ONLY:
                    command = CMD_SNAP;
                    break;
                case WORKING_MODE_UPLOAD_ONLY:
                    command = CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD;
                    if (gpio_rgb_led) {
                        gpio_rgb_led->asyncBlink(60);
                    }
                    break;
                case WORKING_MODE_TEST_ONLY:
                    {
                        auto daynight_state = daynight_switch->getDayNightState();
                        daynight_switch->controlIRCut(daynight_state);
                        daynight_switch->controlIRLed(daynight_state);
                        if (gpio_rgb_led) {
                            gpio_rgb_led->asyncBlink(30);
                        }
                        command = CMD_MOBILE;
                    }
                    break;
                case WORKING_MODE_SNAP_UPLOAD:
                    command = CMD_SNAP | CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD;
                    break;
                case WORKING_MODE_UVC:
                    command = CMD_CONN_NET | CMD_DHCP | CMD_RTSP_SERVER;
                    break;
                default:
                    Logger::log(LogLevel::ERROR, "%s Invalid working mode %d, power off", __func__, working_mode);
                    //Power::getInstance()->requestShutdown();
                    sleep(10);
                    return -1;
            }
        } else {
            Logger::log(LogLevel::ERROR, "%s Invalid command %s, power off", __func__, argv[1]);
            Power::getInstance()->requestShutdown();
            sleep(10);
            return -1;
        }
    }
    
    #if DAEMON_ENABLE
    // 注册到守护服务器
    int pid = getpid();
    int intervalMs = 2000;
    if (registerToDaemonServer(pid, intervalMs)) {
        Logger::log(LogLevel::INFO, "Registered to daemon server with PID=%d, interval=%dms", pid, intervalMs);
    } else {
        Logger::log(LogLevel::WARNING, "Failed to register to daemon server");
    }
    #endif
    
    std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", ""); 
    if (!setting_file_path.empty()) {
        Settings::getInstance()->loadFromJsonFile(setting_file_path);
    }
    auto config = DeviceConfig::getInstance();
    auto program_type = config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, PTYPE_NO_NET);
    Logger::log(LogLevel::INFO, "program type %d", program_type);
    //mount sdcard
#ifdef BUILD_FOR_SIMULATION
    if (!Misc::mountSDCard(simRootPath)) {
        Logger::log(LogLevel::ERROR, "mount sdcard error");
        goto main_exit;
    }
    {
        std::string interface_name = Misc::findUsableNetworkInterface(NETIF_NAME);
        if (interface_name.empty()) {
            interface_name = NETIF_NAME;
        }
        Misc::setNetworkInterfaceName(interface_name);
        elog_i("MDNS", "[SIM] Selected network interface: %s", interface_name.c_str());
    }
#else
    if (!Misc::mountSDCard(SD_CARD_PATH)) {
        Logger::log(LogLevel::ERROR, "mount sdcard error");
        goto main_exit;
    }

    if (program_type == PTYPE_WIFI || command == CMD_MOBILE) {
        Misc::setNetworkInterfaceName(WIFI_IFNAME);
    } else if (program_type == PTYPE_ETHERNET) {
        Misc::setNetworkInterfaceName(ETH_IFNAME);
    } else if (program_type == PTYPE_USB_DONGLE) {
        Misc::setNetworkInterfaceName(USB_DONGLE_IFNAME);
    } else {
        Logger::log(LogLevel::ERROR, "program type %d not support", program_type);
        goto main_exit;
    }

    {
        service::CameraFactoryConfigImporter importer;
        service::CameraFactoryImportResult import_result = importer.importFromSdRoot(SD_CARD_PATH);
        if (import_result.selectedInput.find(service::CameraFactoryConfigImporter::kJsonFileName) != std::string::npos) {
            if (!import_result.success) {
                Json::StreamWriterBuilder writer;
                writer["indentation"] = "";
                Logger::log(LogLevel::ERROR,
                            "Factory JSON import failed: decision=%s errors=%s",
                            import_result.decision.c_str(),
                            Json::writeString(writer, import_result.errors).c_str());
                config->flush_control(false);
                goto main_exit;
            }

            if (import_result.restartRequired) {
                Logger::log(LogLevel::INFO,
                            "Factory JSON import applied from %s, count=%d, restart required",
                            import_result.selectedInput.c_str(),
                            import_result.appliedCount);
                config->flush_control(false);
                goto main_exit;
            }
        }
    }

    //update config
    // First, check if update config file exists by opening it
    {   // Use a scope to ensure file is closed before moving
        std::fstream update_config_file(UPDATE_CONFIG_FILE_PATHNAME, std::ios::in);
        update_config_exists = update_config_file.is_open();
        if (update_config_exists) {
            Logger::log(LogLevel::INFO, "Update config file exists, preparing to update config");
            update_config_file.close();
        }
    }
    
    // Now that file is closed, attempt to move it
    if (update_config_exists) {
        if (!Misc::moveFile(UPDATE_CONFIG_FILE_PATHNAME, CONFIG_FILE_PATHNAME)) {
            Logger::log(LogLevel::ERROR, "Failed to update config file");
        } else {
            Logger::log(LogLevel::INFO, "Successfully updated config file");
            config->flush_control(false);
            goto main_exit;
        }
    }
    //timezone
    timezone = config->get(INI_SECTION_NTP, INI_KEY_TIMEZONE, "");
    if (!timezone.empty()) {
        Logger::log(LogLevel::INFO, "Set timezone to %s", timezone.c_str());
        Timezone::setTimezone(timezone);
    }
    
#endif

    //RTC
    if (command & CMD_GET_RTC) {
        struct tm now;
        if(!RTC::getInstance()->getTime(now)) {
            Logger::log(LogLevel::ERROR, "get RTC time error");
        } else {
            Logger::log(LogLevel::INFO, "RTC time: %d-%02d-%02d %02d:%02d:%02d",
                   now.tm_year+YEAR_OFFSET, now.tm_mon+MONTH_OFFSET, now.tm_mday,
                   now.tm_hour, now.tm_min, now.tm_sec);
        }
    }

    if (command & CMD_SET_RTC) {
        std::string rtc_time = argv[2];
        struct tm timeinfo = {0};
        // Parse the string into struct tm
        if (strptime(rtc_time.c_str(), "%Y-%m-%d %H:%M:%S", &timeinfo) == nullptr) {
            Logger::log(LogLevel::ERROR, "Failed to parse time string: %s", rtc_time.c_str());
        } else {
            // strptime already correctly sets tm_year (years since 1900) and tm_mon (0-11)
            if (!RTC::getInstance()->setTime(timeinfo)) {
                Logger::log(LogLevel::ERROR, "set RTC time: %s error", rtc_time.c_str());
            } else {
                Logger::log(LogLevel::INFO, "set RTC time: %s success", rtc_time.c_str());
            }
        }
    }

    if (command & CMD_SNAP && is_rtc_work_well) {
        if (!processCmdSnap(is_rtc_work_well)) {
            goto main_exit;
        }
    }

    //connect wifi
    if (command & CMD_CONN_NET) {
        if (program_type == PTYPE_WIFI) {
            auto wifi_ssid = config->get(INI_SECTION_SYS, INI_KEY_UPID, "");
            auto wifi_pwd = config->get(INI_SECTION_SYS, INI_KEY_UPWD, "");
            if (wifi_ssid.empty() || wifi_pwd.empty()) {
                Logger::log(LogLevel::ERROR, "wifi ssid or pwd is empty");
                goto main_exit;
            }
            if (!Misc::connectWifi(wifi_ssid, wifi_pwd)) {
                Logger::log(LogLevel::ERROR, "connect wifi error");
                goto main_exit;
            }
        } else if (program_type == PTYPE_USB_DONGLE) {
            auto usb_dongle = UsbDongle::getInstance();
            if (!usb_dongle->loadDriver()) {
                Logger::log(LogLevel::ERROR, "load usb dongle driver error");
                goto main_exit;
            }

            if (!usb_dongle->open()) {
                Logger::log(LogLevel::ERROR, "open usb dongle error");
                goto main_exit;
            }

            if (!usb_dongle->preconfig()) {
                Logger::log(LogLevel::ERROR, "usb dongle preconfig error");
                goto main_exit;
            }
        } else {
            Logger::log(LogLevel::ERROR, "program type %d not support", program_type);
            goto main_exit;
        }
    }

    //dhcp
    if (command & CMD_DHCP) {
        if (!Misc::startDHCP()) {
            Logger::log(LogLevel::ERROR, "start dhcp error");
            goto main_exit;
        }
    }

    if (command & CMD_NTP) {
        auto ntp_server_ip = config->get(INI_SECTION_SERVER, INI_KEY_NTP_IP, "");
        auto ntp_server_port = config->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, 0);
        auto ntp_server = ntp_server_ip + ":" + to_string_custom(ntp_server_port);
        Logger::log(LogLevel::INFO, "ntp server: %s", ntp_server.c_str());
        if (ntp_server.empty()) {
            Logger::log(LogLevel::ERROR, "ntp server is empty");
            goto main_exit;
        }
        if (!Misc::ntpSync(ntp_server)) {
            Logger::log(LogLevel::ERROR, "ntp sync error");
            goto main_exit;
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
            goto main_exit;
        }

        if (is_rtc_work_well) {
            RTC::getInstance()->setTime(*nowtime);
        }
    }

    if (command & CMD_SNAP && !is_rtc_work_well) {
        if (!processCmdSnap(is_rtc_work_well)) {
            goto main_exit;
        }
    }

    if (command & CMD_AUDIO_RECORD) {
        Logger::log(LogLevel::INFO, "[Main] CMD_AUDIO_RECORD enter");
        
        AudioParams audioParam;
        audioParam.setDeviceType(AudioDeviceType::AUDIO_IN);
        audioParam.setDeviceId(1);  
        audioParam.setChannelId(0);
        audioParam.setVolume(80);    
        audioParam.setGain(28);      
        audioParam.setCodecFormat(AudioCodecFormat::AAC);
        #ifdef BUILD_FOR_SIMULATION
        audioParam.setSampleRate(AudioSampleRate::SR_44100);
        #else
        audioParam.setSampleRate(AudioSampleRate::SR_16000);
        #endif
        audioParam.setChannelCount(1);   
        IAudioRecorder* audioIn = new AudioRecorder();
        audioIn->setAudioParams(audioParam);
        audioIn->setRecordFilePath("./res/audioin_record.aac");
        Logger::log(LogLevel::INFO, "[Main] audioIn start...");
        if (audioIn->start()) {
            sleep(20);
            Logger::log(LogLevel::INFO, "[Main] audioIn stop...");
            audioIn->stop();
            Logger::log(LogLevel::INFO, "[Main] audioIn stop done");
        } else {
            Logger::log(LogLevel::ERROR, "audioIn start failed");
        }
        Logger::log(LogLevel::INFO, "[Main] destroyRecorder(audioIn)...");
        delete audioIn;
        Logger::log(LogLevel::INFO, "[Main] destroyRecorder(audioIn) done");
        Logger::log(LogLevel::INFO, "[Main] CMD_AUDIO_RECORD leave");
    }

    if (command & CMD_VIDEO_RECORD) {
        auto videoParam = std::make_shared<VideoParams>();
        videoParam->setResolution(1920, 1080);
        videoParam->setFrameRate(30);
        videoParam->setBitrate(4000000);

        // 配置音频参数
        auto audioParam = std::make_shared<AudioParams>();
        audioParam->setDeviceType(AudioDeviceType::AUDIO_IN);
        audioParam->setDeviceId(1);  
        audioParam->setChannelId(0);
        audioParam->setVolume(80);    
        audioParam->setGain(28);      
        audioParam->setCodecFormat(AudioCodecFormat::AAC);
        #ifdef BUILD_FOR_SIMULATION
        audioParam->setSampleRate(AudioSampleRate::SR_44100);
        #else
        audioParam->setSampleRate(AudioSampleRate::SR_16000);
        #endif
        audioParam->setChannelCount(1);   
    
        auto recorder = std::make_shared<VideoRecorder>(videoParam, audioParam);
        std::string record_path = "./res/" + getCurrentTimeFormatted() + ".mp4";;//MEDIA_STORE_FOLDER_PATH + getCurrentTimeFormatted() + ".mp4";
        Logger::log(LogLevel::INFO, "record to %s", record_path.c_str());
        recorder->record(record_path, 10);
    }

    if (command & CMD_MOBILE) {
        uint16_t http_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_CTRL_PORT, 80);
        uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
        auto wifi_ssid = config->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
        auto wifi_pwd = config->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
#ifndef BUILD_FOR_SIMULATION
        if (wifi_ssid.empty() || wifi_pwd.empty()) {
            Logger::log(LogLevel::ERROR, "wifi ssid or pwd is empty");
            goto main_exit;
        }
        if (!Misc::connectWifi(wifi_ssid, wifi_pwd)) {
            Logger::log(LogLevel::ERROR, "connect wifi error");
            goto main_exit;
        }
        if (!Misc::startDHCP()) {
            Logger::log(LogLevel::ERROR, "start dhcp error");
            goto main_exit;
        }
#endif

        std::string interface_name = Misc::getNetworkInterfaceName();
#ifdef BUILD_FOR_SIMULATION
        std::string detected_interface = Misc::findUsableNetworkInterface(interface_name);
        if (!detected_interface.empty() && detected_interface != interface_name) {
            interface_name = detected_interface;
            Misc::setNetworkInterfaceName(interface_name);
        }
#endif

        std::string ip_address = Misc::getIPAddress(interface_name);
        if (ip_address.empty()) {
            Logger::log(LogLevel::ERROR, "No IP address found on interface %s", interface_name.c_str());
            goto main_exit;
        }

        if (isMdnsEnabled(config)) {
            auto mdns_params = buildMdnsParams(config, interface_name, ip_address, http_port, rtsp_port);
            if (!service::MdnsService::getInstance()->start(mdns_params)) {
                Logger::log(LogLevel::ERROR, "Failed to start mDNS service");
                goto main_exit;
            }
        } else {
            elog_i("MDNS", "mDNS disabled by config");
        }

        // 启动 HTTP Server 替代 RemoteCtrlClient
        HttpServerConfig httpConfig = {static_cast<int>(http_port), nullptr, 2};
        if (http_server_init(&httpConfig) != 0) {
            Logger::log(LogLevel::ERROR, "Failed to init HTTP server");
            service::MdnsService::getInstance()->stop();
            goto main_exit;
        }
        if (http_server_start() != 0) {
            Logger::log(LogLevel::ERROR, "Failed to start HTTP server");
            http_server_deinit();
            service::MdnsService::getInstance()->stop();
            goto main_exit;
        }
        if (!service::TcpEventService::getInstance()->start(service::kDefaultTcpEventPort)) {
            Logger::log(LogLevel::WARNING, "Failed to start TCP event server on port %u",
                        service::kDefaultTcpEventPort);
        }
        elog_i("MDNS", "HTTP server started on port %u for interface %s (%s)",
               http_port, interface_name.c_str(), ip_address.c_str());
        
        RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
            Logger::log(LogLevel::INFO, "RTSP session closed in mobile mode, waiting for new connection...");
        });
        RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
        if (!RtspServer::getInstance()->start()) {
            Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
            if (http_server_is_running()) {
                http_server_stop();
                http_server_deinit();
            }
            service::TcpEventService::getInstance()->stop();
            service::MdnsService::getInstance()->stop();
            goto main_exit;
        }

        while (!already_in_exit_flow) {
            sleep(1);
        }

        service::MdnsService::getInstance()->stop();
        
        // 停止 RTSP Server
        RtspServer::getInstance()->stop();
        
        // 停止 HTTP Server
        if (http_server_is_running()) {
            http_server_stop();
            http_server_deinit();
        }
        service::TcpEventService::getInstance()->stop();
    }

    if (command & CMD_RTSP_SERVER) {
        //auto wifi_ssid = config->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
        //auto wifi_pwd = config->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
        //Misc::connectWifi(wifi_ssid, wifi_pwd);
        //Misc::startDHCP();
        uint16_t rtsp_port = getConfiguredPort(config, INI_SECTION_MDNS, INI_KEY_MDNS_RTSP_PORT, DEFAULT_RTSP_PORT);
        RtspServer::getInstance()->registerOnsessionClosedCallback([]() {
            Logger::log(LogLevel::INFO, "RTSP session closed, waiting for new connection...");
        });
        RtspServer::getInstance()->setPort(static_cast<int>(rtsp_port));
        if (!RtspServer::getInstance()->start()) {
            Logger::log(LogLevel::ERROR, "Failed to start RTSP server");
            goto main_exit;
        }
        /* RTSP 服务器持续运行，等待退出信号 */
        while (!already_in_exit_flow) {
            sleep(1);
        }
        RtspServer::getInstance()->stop();
    }

    if (command & CMD_AUTH || command & CMD_HEARTBEAT || command & CMD_UPLOAD) {
        auto mgmtServerAddr = config->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "");
        Logger::log(LogLevel::INFO, "mgmtServerAddr: %s", mgmtServerAddr.c_str());
        auto mgmtServerPort = config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, 0);
        Logger::log(LogLevel::INFO, "mgmtServerPort: %d", mgmtServerPort);

        mgmtServClient = std::make_shared<MgmtServClient>(mgmtServerAddr, mgmtServerPort);
        if (EC_SUCCESS != mgmtServClient->connect(3000)) {
            Logger::log(LogLevel::ERROR, "connect [%s:%d] failed", mgmtServerAddr.c_str(), mgmtServerPort);
            goto main_exit;
        } else {
            Logger::log(LogLevel::INFO, "connect [%s:%d] success", mgmtServerAddr.c_str(), mgmtServerPort);
        }

        if (EC_SUCCESS != mgmtServClient->authenticate()) {
            Logger::log(LogLevel::ERROR, "auth failed");
            goto main_exit;
        } else {
            Logger::log(LogLevel::INFO, "auth success");
        }
    }

    if (command & CMD_HEARTBEAT) {
        mgmtServClient->sendHeartbeat();
    }
    
    if (command & CMD_UPLOAD) {
        //assume we have a storage server same as mgmt server
        storageServClient = mgmtServClient->newStorageServClient();
        if (gpio_rgb_led) {
            gpio_rgb_led->setConstant(GPIO_VALUE::HIGH);
        }
        bool descfile_uploaded = false;
        auto desc_filenames = Misc::listFilenames(MEDIA_UPLOAD_PATH);
        for (auto &desc_filename : desc_filenames) {
            desc_filename = MEDIA_UPLOAD_PATH + desc_filename;
            Logger::log(LogLevel::INFO, "desc_filename %s", desc_filename.c_str());
            std::ifstream ifs(desc_filename);
            
            if (!Misc::isJsonFile(desc_filename)) {
                Logger::log(LogLevel::INFO, "%s is not json", desc_filename.c_str());
                Misc::deleteFile(desc_filename);
                continue;
            }

            Json::Value root;
            Json::Reader reader;
            if (!reader.parse(ifs, root)) {
                Logger::log(LogLevel::ERROR, "Failed to parse JSON file: %s", desc_filename.c_str());
                Misc::deleteFile(desc_filename);
                continue;
            }

            std::string pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
            if (!root.isMember("F_UploadedTag") ||!root.isMember("device") || !root["device"].isMember("PID") || (root["device"]["PID"].asString() != pid)) {
                Logger::log(LogLevel::ERROR, "PID not match");
                Misc::deleteFile(desc_filename);
                continue;
            }
            
            if (!root.isMember("file_inf")) {
                Logger::log(LogLevel::ERROR, "file_inf not exist");
                Misc::deleteFile(desc_filename);
                continue;
            }
            
            descfile_uploaded = false;
            if (root["F_UploadedTag"].asInt() == 0) {
                //upload file description json file
                Logger::log(LogLevel::INFO, "original desc_filename %s", desc_filename.c_str());
                auto target_filename = desc_filename;
                storageServClient->bindUploadCallback([&descfile_uploaded, target_filename](const std::string &filename, int error_code) {
                    Logger::log(LogLevel::INFO, "upload descfile [%s], error code: %d", filename.c_str(), error_code);
                    if (filename == target_filename) {
                        descfile_uploaded = (error_code==EC_SUCCESS)?true:false;
                    }
                });
                storageServClient->uploadFile(desc_filename);
                auto start_time = std::chrono::steady_clock::now();
                auto now = std::chrono::steady_clock::now();
                while (std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count() < 8) {//must greater than 5s
                    if (descfile_uploaded) {
                        Logger::log(LogLevel::INFO, "descfile %s uploaded", desc_filename.c_str());
                        root["F_UploadedTag"] = 1;
                        std::ofstream ofs(desc_filename);
                        ofs << root.toStyledString();
                        ofs.close();
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    now = std::chrono::steady_clock::now();
                }
            } else {
                descfile_uploaded = true;
            }

            if (descfile_uploaded) {
                bool allFileUploaded = true;
                const Json::Value file_inf_array = root["file_inf"];
                std::vector<std::string> uploaded_file_list;
                storageServClient->bindUploadCallback([&uploaded_file_list, &allFileUploaded](const std::string &filename, int error_code) {
                    Logger::log(LogLevel::INFO, "upload %s, error code: %d", filename.c_str(), error_code);
                    if (error_code == EC_SUCCESS) {
                        uploaded_file_list.push_back(filename);
                    } else {
                        allFileUploaded = false;
                    }
                });
                for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
                    if (file_inf_array[i].isMember("F_FileName") && file_inf_array[i].isMember("F_FilePath")) {
                        std::string filepath = file_inf_array[i]["F_FilePath"].asString();
                        std::string filename = file_inf_array[i]["F_FileName"].asString();
                        auto tag = file_inf_array[i]["F_UploadedTag"].asInt();
                        if (tag == 0) {
                            Logger::log(LogLevel::INFO, "uploading file: %s", filename.c_str());
                            auto pathname = filepath + "/" + filename;
                            storageServClient->uploadFile(pathname);
                        }
                    }
                }

                while (!storageServClient->isUploadFinished()) {
                    usleep(1);
                }

                for (auto& filename : uploaded_file_list) {
                    for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
                        auto pathname = file_inf_array[i]["F_FilePath"].asString() + "/" + file_inf_array[i]["F_FileName"].asString();
                        if (pathname == filename) {
                            root["file_inf"][i]["F_UploadedTag"] = 1;
                            
                            auto file_manage_type = config->get(INI_SECTION_POLICY, INI_KEY_FILE_MANAGE, 0);
                            if (file_manage_type == FILE_MANAGE_DELETE) {
                                Misc::deleteFile(pathname);
                            }
                        }
                    }
                }

                std::ofstream ofs(desc_filename);
                ofs << root.toStyledString();
                ofs.close();
                if (allFileUploaded) {
                    Logger::log(LogLevel::INFO, "upload all files finished in %s", desc_filename.c_str());
                    //Misc::deleteFile(desc_filename);
                }
            }    
        }
    }

main_exit:
    service::TcpEventService::getInstance()->stop();
    service::MdnsService::getInstance()->stop();
    // 停止信号处理工作线程
    if (signalHandlerThread.joinable()) {
        Logger::log(LogLevel::INFO, "Stopping signal handler thread...");
        // 发送停止线程的消息
        {
            std::lock_guard<std::mutex> lock(messageMutex);
            signalMessageQueue.push({SignalMessageType::STOP_THREAD});
        }
        messageCondition.notify_one();
        
        // 等待线程结束
        signalHandlerThread.join();
        Logger::log(LogLevel::INFO, "Signal handler thread stopped");
    }
    
    Settings::getInstance()->saveToJsonFile(setting_file_path);
    
    Logger::log(LogLevel::INFO, "Power off From Main function");
#ifndef BUILD_FOR_SIMULATION
    auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
    if (!gpio_power_hold.exportGPIO() || !gpio_power_hold.setDirection(GPIO_DIRECTION::OUTPUT)
        || !gpio_power_hold.setValue(GPIO_VALUE::LOW)) {
        Logger::log(LogLevel::ERROR, "%s Failed to set power hold pin", __func__);
    }
#endif
    auto_release.release();
#ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "[SIM] Program exit normally");
    _exit(0);
#else
    syncWithMCU();
    config->flush();
    //Misc::poweroff();
    //while(1);
    return 0;
#endif
}
