
#include <iostream>
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
#include "StringConvert.h"
#include "system_call.h"
#include "app.h"
#include "Common.h"
using namespace media;

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

	ret = system_call((char*)command.c_str(), 5000);
	if(ret < 0) {
        Logger::log(LogLevel::ERROR, "call %s error", command.c_str());
	}

    system_call_exit();

    return ret < 0 ? false : true;
}

int main(int argc, char* argv[])
{
    //show timestamp
    struct timespec ts0;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts0) != -1) {
        Logger::log(LogLevel::INFO, "main entry at %ld ms", ts0.tv_sec * 1000 + ts0.tv_nsec / 1000000);
    }

    auto dirPath = std::string(QUICK_SNAP_DIR);
    if (!createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }
    #if RTC_EXIST
    std::string timeStr = getCurrentTimeFormatted();
    dirPath += timeStr;
    #else
    dirPath += "pic";
    #endif
    if (!createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }

    if (!EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME)) {
        Logger::log(LogLevel::ERROR, "Failed to parse env file: %s", ENV_FILE_PATHNAME);
        return -1;
    }
    std::string setting_file_path = EnvManager::getInstance()->getEnv("SETTING_FILE_PATH", ""); 
    if (setting_file_path.empty()) {
        Logger::log(LogLevel::ERROR, "Failed to get setting file path");
        return -1;
    }
    
    bool load_success = Settings::getInstance()->loadFromJsonFile(setting_file_path);
    if (!load_success) {
        Logger::log(LogLevel::ERROR, "Failed to load setting file: %s", setting_file_path.c_str());
        return -1;
    }

    auto burstNumber = Settings::getInstance()->burstNumber;
    if (burstNumber <= 0) {
        Logger::log(LogLevel::ERROR, "Invalid burst number: %d", burstNumber);
        return -1;
    }

    std::vector<std::string> fileNames;
    for (int i = 0; i < burstNumber; i++) {
        #if RTC_EXIST
        fileNames.push_back(dirPath + "/" + timeStr + "_" + to_string_custom(i + 1) + ".JPG");
        #else
        fileNames.push_back(dirPath + "/" + to_string_custom(i + 1) + ".JPG");
        #endif
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
    #if RTC_EXIST
    root["files"] = imageArray;
    root["dir"] = timeStr;
    #else
    root["files"] = imageArray;
    root["dir"] = "pic";
    #endif
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
    
    // call htc_main_app
    startApp("htc_main_app -qs");
    return 0;
}
