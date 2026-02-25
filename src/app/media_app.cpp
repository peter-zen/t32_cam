
#include <iostream>
#include <sys/time.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <json/json.h>
#include "ImageSnap.h"
#include "EnvManager.h"
#include "Settings.h"
#include "Logger.h"
#include "utils/string/StringConvert.h"
#include "system_call.h"
#include "DeviceConfig.h"
#include "app.h"
#include "Common.h"
#include "workmode/WorkMode.h"
#include "utils/AutoRelease.h"
#include "time/rtc/RTC.h"
#include "../service/daemon/DeamonClient.h"
#include "time/timezone/Timezone.h"

using namespace media;

static bool isRTCWorkWell()
{
    #if RTC_EXIST
        struct tm nowtime;
        if (!RTC::getInstance()->getTime(nowtime)) {
            return false;
        }
        // Convert tm to timeval and set system time
        time_t time_in_sec = mktime(&nowtime);
        if (time_in_sec != -1) {
            struct timeval tv;
            tv.tv_sec = time_in_sec;
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
        }
        return true;
    #else
        return false;
    #endif
}

static std::string getCurrentTimeFormatted()
{
    time_t now = time(nullptr);
    struct tm* time_info = localtime(&now);
    
    std::stringstream ss;
    ss << std::put_time(time_info, "%Y%m%d_%H%M%S");
    return ss.str();
}

static bool createDirectory(const std::string& path, mode_t mode)
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

static bool startApp(const std::string& command)
{
    int ret = 0;
    ret = system_call_init();
    if(ret < 0) {
        Logger::log(LogLevel::ERROR, "system_call_init failed");
        return false;
    }

	ret = system_call((char*)command.c_str(), 500);
	if(ret < 0) {
        Logger::log(LogLevel::ERROR, "call %s error", command.c_str());
	}

    system_call_exit();

    return ret < 0 ? false : true;
}

int quick_snap(bool is_rtc_work_well)
{
    auto dirPath = std::string(QUICK_SNAP_DIR);
    if (!createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }

    std::string timeStr = getCurrentTimeFormatted();
    if (is_rtc_work_well) {
        dirPath += timeStr;
    } else {
        dirPath += "pic";
    }

    if (!createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }

    auto burstNumber = Settings::getInstance()->burstNumber;
    if (burstNumber <= 0) {
        Logger::log(LogLevel::ERROR, "Invalid burst number: %d", burstNumber);
        return -1;
    }

    std::vector<std::string> fileNames;
    for (int i = 0; i < burstNumber; i++) {
        if (is_rtc_work_well) {
            fileNames.push_back(dirPath + "/" + timeStr + "_" + to_string_custom(i + 1) + ".JPG");
        } else {
            fileNames.push_back(dirPath + "/" + to_string_custom(i + 1) + ".JPG");
        }
    }
    auto snapSizeIndex = Settings::getInstance()->stillSize;
    auto snap_param = ImageSnapParams();
    snap_param.setImageSize(SnapImgSize[snapSizeIndex].width, SnapImgSize[snapSizeIndex].height);
    Logger::log(LogLevel::INFO, "Snap image size: %d x %d", SnapImgSize[snapSizeIndex].width, SnapImgSize[snapSizeIndex].height);
    auto imageSnap = std::make_shared<ImageSnap>(snap_param);
    if (!imageSnap->snap(fileNames)) {
        Logger::log(LogLevel::ERROR, "Failed to snap images");
        return -1;
    }
    
    //show timestamp
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != -1) {
        Logger::log(LogLevel::INFO, "capture done at %ld ms", ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    }
    
    Json::Value root;
    Json::Value imageArray(Json::arrayValue);
    for (const auto& fileName : fileNames) {
        imageArray.append(std::string(fileName.substr(fileName.find_last_of("/") + 1)));
    }
    if (is_rtc_work_well) {
        root["files"] = imageArray;
        root["dir"] = timeStr;
    } else {
        root["files"] = imageArray;
        root["dir"] = "pic";
    }
    std::string jsonFilePath = std::string(QUICK_SNAP_DIR) + "info.json";
    std::ofstream jsonFile(jsonFilePath);
    if (!jsonFile.is_open()) {
        Logger::log(LogLevel::ERROR, "Failed to open file: %s", jsonFilePath.c_str());
        return -1;
    }

    Json::StreamWriterBuilder writerBuilder;
    std::unique_ptr<Json::StreamWriter> jsonWriter(writerBuilder.newStreamWriter());
    jsonWriter->write(root, &jsonFile);
    jsonFile.close();

    return 0;
}

int main(int argc, char* argv[])
{
    bool rtc_work_well = true;
    bool load_success = false;
    auto working_mode = workingMode::WORKING_MODE_MAX;
    #if !UVC_ENABLE
    std::string setting_file_path;
    //show timestamp
    struct timespec ts0;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts0) != -1) {
        Logger::log(LogLevel::INFO, "main entry at %ld ms", ts0.tv_sec * 1000 + ts0.tv_nsec / 1000000);
    }

    {
        auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
        if (!gpio_power_hold.exportGPIO() || !gpio_power_hold.setDirection(GPIO_DIRECTION::OUTPUT)
            || !gpio_power_hold.setValue(GPIO_VALUE::HIGH)) {
            Logger::log(LogLevel::ERROR, "Failed to set power hold pin");
            goto main_exit;
        }
    }

    if (EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME)) {
        auto config = DeviceConfig::getInstance();
        std::string timezone = config->get(INI_SECTION_NTP, INI_KEY_TIMEZONE, "");
        if (!timezone.empty()) {
            Logger::log(LogLevel::INFO, "Set timezone to %s", timezone.c_str());
            Timezone::setTimezone(timezone);
        }
        setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", ""); 
        if (!setting_file_path.empty()) {
            load_success = Settings::getInstance()->loadFromJsonFile(setting_file_path);
            if (load_success) {
                Logger::log(LogLevel::INFO, "force_upload = %d", Settings::getInstance()->force_upload);
                if (Settings::getInstance()->force_upload == 1) {
                    working_mode = workingMode::WORKING_MODE_UPLOAD_ONLY;
                    Settings::getInstance()->force_upload = 0;
                    Settings::getInstance()->saveToJsonFile(setting_file_path);
                    goto main_exit;
                }
            }
        }
    }

    rtc_work_well = isRTCWorkWell();

    //get working mode
    #if !MCU_EXIST
    WorkMode::setWorkingModePins(WORKING_MODE_CHECK_PIN_0, WORKING_MODE_CHECK_PIN_1);
    #endif
    working_mode = WorkMode::getWorkingMode();

    if (working_mode == workingMode::WORKING_MODE_SNAP_ONLY || working_mode == workingMode::WORKING_MODE_SNAP_UPLOAD) {
        if (quick_snap(rtc_work_well) < 0) {
            Logger::log(LogLevel::ERROR, "Failed to quick snap");
            working_mode = workingMode::WORKING_MODE_MAX;
        }
    }
#else
    working_mode = workingMode::WORKING_MODE_UVC;
#endif

main_exit:
    #if DAEMON_ENABLE
    startApp("htc_daemon_app &");
    #endif
    // call htc_main_app
    std::string command = "htc_main_app -wm " + to_string_custom((int)working_mode) + " -rtc " + to_string_custom(rtc_work_well);
    startApp(command);
    return 0;
}
