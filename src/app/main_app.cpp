#include <iostream>
#include <fstream>
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <sys/stat.h>
#include <iomanip>
#include <sys/time.h>
#include <json/json.h>
#include "MgmtServClient.h"
#include "RemoteCtrlClient.h"
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "ImageSnap.h"
#include "EnvManager.h"
#include "Misc.h"
#include "CRC.h"
#include "Settings.h"
#include "MCU.h"
#include "Disk.h"

using namespace network;
using namespace media;

#define QUICK_SNAP_DIR   "/tmp/quick_snap/"
#define QUICK_SNAP_INFO_FILE   QUICK_SNAP_DIR"info.json"
#define SD_CARD_PATH   "/mnt/sdcard/"
#define MEDIA_TARGET_PATH   SD_CARD_PATH"media/"
#define MEDIA_UPLOAD_PATH   SD_CARD_PATH"media/upload/"
#define NETIF_NAME "wlan0"

static bool getFileCreationTime(const std::string& filename, std::string& time_str)
{
    char tmp_buffer[64];
    struct stat attr;
    if (stat(filename.c_str(), &attr) == 0) {
        std::tm* tm_info = std::localtime(&attr.st_ctime);
        
        if (!std::strftime(tmp_buffer, sizeof(tmp_buffer), "%Y-%m-%dT%H:%M:%S.000+08:00", tm_info)) {
            Logger::log(LogLevel::ERROR, "Failed to get file creation time");
            return false;
        }
        time_str = tmp_buffer;
    } else {
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
        snprintf(tmp_buffer, sizeof(tmp_buffer), "%s-%s-%sT%s:%s:%s.000+08:00", year, mon, day, hour, min, sec);

        time_str = tmp_buffer;
    }

    return true;
}



static int generateDescInfo(std::vector<std::string>& files, std::string& desc_info)
{
    auto settings = Settings::getInstance();
    char temp_buf[32] = {0};
    auto mcu = MCU::getInstance();
    struct timeval tv;
	gettimeofday(&tv, nullptr);
    std::ostringstream oss;
	oss << std::put_time(localtime(&tv.tv_sec), "%Y-%m-%dT%H:%M:%S.000+08:00");
	std::string current_time_str = oss.str();

    int val = 0;

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
        auto battery_volte = mcu->readBatteryVoltage();
        snprintf(temp_buf, sizeof(temp_buf), "%d.%0d", battery_volte / 1000, (battery_volte % 1000) / 100);
        device_obj["Battery1"] = temp_buf;
        device_obj["Battery2"] = "0";

        auto ext_volte = mcu->readExternalVoltage();
        auto shutdown_volte = mcu->readShutdownVoltage();
        if ( ext_volte <= 14000 || battery_volte <= shutdown_volte ) {
            device_obj["SPower"] = "0";
            snprintf(temp_buf, sizeof(temp_buf), "%d.%0d", ext_volte / 1000, (ext_volte % 1000) / 100);
            device_obj["EPower"] = temp_buf;
        }
        else
        {
            device_obj["EPower"] = "0";
            snprintf(temp_buf, sizeof(temp_buf), "%d.%0d", ext_volte / 1000, (ext_volte % 1000) / 100 );
            device_obj["SPower"] = temp_buf;
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
        else if ( battery_volte <= shutdown_volte && ext_volte <= shutdown_volte ) {
            device_obj["AStatus"] = 23;  /*powroff*/
        }
        else if ( battery_volte <= lowpower_volte && ext_volte <= lowpower_volte  ) {
            device_obj["AStatus"] = 21;  /*low*/
        }
        else {
            device_obj["AStatus"] = 11;
        }

        auto battery_level = mcu->readBatteryLevel();
        device_obj["BAT1_Level"] = battery_level;

        
        device_obj["Low_PWR_Val"] = lowpower_volte;;
        snprintf(temp_buf, sizeof(temp_buf), "%d.%0d", lowpower_volte / 1000, (lowpower_volte % 1000) / 100 );
        device_obj["Low_PWR_Val"] = temp_buf;

        snprintf(temp_buf, sizeof(temp_buf), "%d.%0d", shutdown_volte / 1000, (shutdown_volte % 1000) / 100 );
        device_obj["Loff_PWR_Val"] = temp_buf;

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
        auto temperature = mcu->readTemperature();
        snprintf(temp_buf, sizeof(temp_buf), "%d.%0d", temperature / 10, abs( temperature % 10 ) );
        data_obj["D_Temperature"] = temp_buf;
        data_obj["D_Humidity"] = "0";
        data_obj["D_Atmos"] = "0";

        snprintf(temp_buf, sizeof(temp_buf), "%08X", mcu->readRMID());
        data_obj["D_SensorPID"] = temp_buf;
        data_obj["D_SensorType"] = mcu->readRMType();
        data_obj["D_SensorValue"] = mcu->readRMValue();
        auto rm_bat_v = mcu->readRMBatteryValue();
        snprintf(temp_buf, sizeof(temp_buf), "%d.%d", rm_bat_v / 10, abs( rm_bat_v % 10 ));
        data_obj["D_SensorBattery"] = temp_buf;
        data_obj["D_SensorGP"] = "";
        data_obj["D_SensorCount"] = mcu->readRMCount();
        auto rm_sp_v = mcu->readRMSunPowerValue();
        snprintf(temp_buf, sizeof(temp_buf), "%d.%d", rm_sp_v / 10, abs( rm_sp_v % 10 ));
        data_obj["D_SensorSP"] = temp_buf;
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

static void printUsage(char *argv[])
{
    std::cout << "Usage: " << argv[0] << " <command> [options]" << std::endl;
    std::cout << "Commands:" << std::endl;
    std::cout << "  -h, --help\t\tDisplay this help message" << std::endl;
    std::cout << "  -a, --auth\t\tAuthenticate with the management server" << std::endl;
    std::cout << "  -hb, --heartbeat\tSend a heartbeat message to the management server" << std::endl;
    std::cout << "  -s, --snap\t\tSnap an image" << std::endl;
    std::cout << "  -u, --upload\t\tUpload a file to the storage server" << std::endl;
}

enum {
    CMD_HELP = 0,
    CMD_CONNECT_WIFI,
    CMD_DHCP,
    CMD_SNAP,
    CMD_AUTH,
    CMD_HEARTBEAT,
    CMD_UPLOAD,
    CMD_MOBILE,
};

int main(int argc, char* argv[])
{
    int command = CMD_HELP;
    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {  
        printUsage(argv);
        return -1;
    }

    if (std::string(argv[1]) == "-w" || std::string(argv[1]) == "--wifi") {
        command = CMD_CONNECT_WIFI;
    } else if (std::string(argv[1]) == "-d" || std::string(argv[1]) == "--dhcp") { 
        command = CMD_DHCP;
    } else if (std::string(argv[1]) == "-s" || std::string(argv[1]) == "--snap") {
        command = CMD_SNAP;
    } else if (std::string(argv[1]) == "-a" || std::string(argv[1]) == "--auth") { 
        command = CMD_AUTH;
    } else if (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--heartbeat") {
        command = CMD_HEARTBEAT;
    } else if (std::string(argv[1]) == "-u" || std::string(argv[1]) == "--upload") {
        command = CMD_UPLOAD;
    } else if (std::string(argv[1]) == "-m" || std::string(argv[1]) == "--mobile") {
        command = CMD_MOBILE;
    } else {
        printUsage(argv);
        return -1;
    }

    Misc::setNetworkInterfaceName(NETIF_NAME);
    EnvManager::getInstance()->parsePrimaryEnv("./res/env.ini");
    auto config = DeviceConfig::getInstance();
    std::shared_ptr<MgmtServClient> mgmtServClient;
    if (command == CMD_CONNECT_WIFI) {
        auto wifi_ssid = config->get(INI_SECTION_SYS, INI_KEY_UPID, "");
        auto wifi_pwd = config->get(INI_SECTION_SYS, INI_KEY_PWD, "");
        Misc::connectWifi(wifi_ssid, wifi_pwd);
        Misc::startDHCP();
    } else if (command == CMD_DHCP) {
        Misc::startDHCP();
    }

    if (command == CMD_SNAP) {
        //mount sdcard
        if (!Misc::mountSDCard(SD_CARD_PATH)) {
            std::cout << "mount sdcard error" << std::endl;
            return -1;
        }
        
        //move media file from /tmp to sdcard
        std::vector<std::string> file_names;
       
        std::ifstream jsonFile(QUICK_SNAP_INFO_FILE);
        if (jsonFile.is_open()) {
            Json::Value root;
            Json::CharReaderBuilder readerBuilder;
            std::string errs;
            if (!Json::parseFromStream(readerBuilder, jsonFile, &root, &errs)) {
                Logger::log(LogLevel::ERROR, "Parse json file failed");
                return -1;
            } 
            auto dir = root["dir"].asString();
            auto files = root["files"];
            std::string oldpath = QUICK_SNAP_DIR + dir + "/*";
            std::string newpath =  MEDIA_TARGET_PATH + dir;
            std::string upload_path = MEDIA_UPLOAD_PATH + dir;

            if (!Misc::createDirectory(newpath) || !Misc::moveFile(oldpath, newpath)) {
                Logger::log(LogLevel::ERROR, "move %s to %s failed", oldpath.c_str(), newpath.c_str());
                return -1;
            }

            //create desc file
            for (auto & file : files) { 
                auto filename = newpath + "/" + file.asString();
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
    }

    if (command == CMD_MOBILE) {
        auto wifi_ssid = config->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "");
        auto wifi_pwd = config->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "");
        Misc::connectWifi(wifi_ssid, wifi_pwd);
        Misc::startDHCP();
        auto remoteCtrlServerIp = Misc::getGatewayAddress(Misc::getNetworkInterfaceName());
        if (remoteCtrlServerIp.empty()) {
            std::cout << "Failed to get gateway IP address" << std::endl;
            return -1;
        }
        auto remoteCtrlServerPort = 7788;
        auto remoteCtrlClient = std::make_shared<RemoteCtrlClient>(remoteCtrlServerIp, 7788);
        if (EC_SUCCESS != remoteCtrlClient->connect(3000)) {
            Logger::log(LogLevel::ERROR, "connect [%s:%d] failed", remoteCtrlServerIp.c_str(), 7788);
            return -1;
        }

        sleep(20);
    }
    if (command == CMD_AUTH || command == CMD_HEARTBEAT || command == CMD_UPLOAD) {
        auto mgmtServerAddr = config->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "");
        Logger::log(LogLevel::INFO, "mgmtServerAddr: %s", mgmtServerAddr);
        auto mgmtServerPort = config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, 0);
        Logger::log(LogLevel::INFO, "mgmtServerPort: %d", mgmtServerPort);

        mgmtServClient = std::make_shared<MgmtServClient>(mgmtServerAddr, mgmtServerPort);
        if (EC_SUCCESS != mgmtServClient->connect(3000)) {
            Logger::log(LogLevel::ERROR, "connect [%s:%d] failed", mgmtServerAddr, mgmtServerPort);
            return -1;
        } else {
            Logger::log(LogLevel::INFO, "connect [%s:%d] success", mgmtServerAddr, mgmtServerPort);
        }

        if (EC_SUCCESS != mgmtServClient->authenticate()) {
            Logger::log(LogLevel::ERROR, "auth failed");
            return -1;
        } else {
            Logger::log(LogLevel::INFO, "auth success");
        }

        if (command == CMD_AUTH) {
            sleep(10);
        }
    }

    if (command == CMD_HEARTBEAT) {
        mgmtServClient->sendHeartbeat();
    }
    
    if (command == CMD_UPLOAD) {
        //assume we have a storage server same as mgmt server
        auto storageServClient = mgmtServClient->newStorageServClient();
        storageServClient->bindUploadCallback([](const std::string &filename, int error_code) {
            Logger::log(LogLevel::INFO, "upload %s, error code: %d", filename.c_str(), error_code);
        });
        auto desc_filenames = Misc::listFilenames(MEDIA_UPLOAD_PATH);
        for (auto &desc_filename : desc_filenames) {
            Logger::log(LogLevel::INFO, "desc_filename %s", desc_filename.c_str());
            if (!Misc::isJsonFile(MEDIA_UPLOAD_PATH + desc_filename)) {
                Logger::log(LogLevel::INFO, "%s is not json", desc_filename.c_str());
                continue;
            }
            
            //upload file description json file
            storageServClient->uploadFile(desc_filename);
            
            //upload files recorded in json file
            std::ifstream ifs(desc_filename);
            
            if (!ifs.is_open()) {
                Logger::log(LogLevel::ERROR, "Failed to open JSON file: %s", desc_filename.c_str());
            } else {
                Json::Value root;
                Json::Reader reader;
                if (!reader.parse(ifs, root)) {
                    Logger::log(LogLevel::ERROR, "Failed to parse JSON file: %s", desc_filename.c_str());
                } else {
                    std::string pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
                    if (!root.isMember("device") || !root["device"].isMember("PID") || (root["device"]["PID"].asString() != pid)) {
                        Logger::log(LogLevel::ERROR, "PID not match");
                        return -1;
                    }
                    
                    if (!root.isMember("file_inf")) {
                        Logger::log(LogLevel::ERROR, "file_inf not exist");
                        return -1;
                    }
                    
                    const Json::Value file_inf_array = root["file_inf"];
                    for (Json::ArrayIndex i = 0; i < file_inf_array.size(); ++i) {
                        if (file_inf_array[i].isMember("F_FileName") && file_inf_array[i].isMember("F_FilePath")) {
                            std::string filepath = file_inf_array[i]["F_FilePath"].asString();
                            std::string filename = file_inf_array[i]["F_FileName"].asString();
                            Logger::log(LogLevel::INFO, "uploading file: %s", filename.c_str());
                            auto pathname = filepath + "/" + filename;
                            storageServClient->uploadFile(pathname);
                        }
                    }

                    while (!storageServClient->isUploadFinished()) {
                        usleep(1);
                    }
                    Logger::log(LogLevel::INFO, "upload all files finished");
                }
            }
        }
        
    }

    return 0;
}