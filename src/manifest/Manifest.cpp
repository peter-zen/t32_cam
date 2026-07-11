#include "manifest/Manifest.h"

#include <string>
#include <vector>
#include <cstring>     // memset/strncpy
#include <cstdlib>     // atoi
#include <sys/stat.h>  // stat
#include <sys/time.h>  // gettimeofday, struct timeval
#include <cstdio>      // fopen/fwrite/fclose, snprintf

#include <json/json.h>

#include "Common.h"                       // YEAR_OFFSET, MONTH_OFFSET, DISK_PATHNAME,
                                          // EC_SUCCESS, EC_OPEN_FILE_FAILED, PTYPE_USB_DONGLE
#include "Logger.h"
#include "StringConvert.h"                // to_string_custom (header-only template)
#include "misc/Misc.h"                    // Misc::getFilepath/getFilename/getIPAddress/getNetworkInterfaceName
#include "utils/crc/CRC.h"                // CRC::calculate_crc16
#include "Timezone.h"                     // Timezone::getFormattedTimeWithTimezone
#include "Settings.h"                     // Settings::getInstance()
#include "MCU.h"                          // MCU::getInstance()
#include "DeviceConfig.h"                 // DeviceConfig::getInstance()
#include "ProductConfig.h"                // ProductConfig::getInstance() (BOOT fields)
#include "Disk.h"                         // Disk::getInfo

namespace manifest {

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

int generateDescInfo(std::vector<std::string>& files, std::string& desc_info)
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
        // T25 Phase-3: UPID migrated from DeviceConfig SYSTEM to Settings
        network_obj["N_UPID"] = Settings::getInstance()->upid.empty()
                                    ? std::string("CKVISON")
                                    : Settings::getInstance()->upid;
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
            auto program_type = ProductConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0);
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

int createDescInfoFile(std::vector<std::string>& media_files, const std::string &desc_filename)
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

} // namespace manifest
