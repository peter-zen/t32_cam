
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
#include "Logger.h"
#include "system_call.h"

using namespace media;

#define QUICK_SNAP_DIR   "/tmp/quick_snap/"

std::string getCurrentTimeFormatted()
{
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
    return ss.str();
}

bool createDirectory(const std::string& path, mode_t mode)
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

bool startApp(const std::string& command)
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
    auto dirPath = std::string(QUICK_SNAP_DIR);
    if (!createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }
    std::string timeStr = getCurrentTimeFormatted();
    dirPath += timeStr;
    if (!createDirectory(dirPath, 0777)) {
        Logger::log(LogLevel::ERROR, "Failed to create directory: %s", dirPath.c_str());
        return -1;
    }
    
    std::vector<std::string> fileNames = {
        dirPath + "/" + timeStr + "_1.JPG",
        //dirPath + "/" + timeStr + "_2.JPG",
        //dirPath + "/" + timeStr + "_3.JPG",
    };
    
    auto snap_param = ImageSnapParams();
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
    root["files"] = imageArray;
    root["dir"] = timeStr;
    
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
    //startApp("/mnt/huntcam/bin/htc_main_app --snap");
    startApp("/usr/bin/htc_main_app --snap");
    return 0;
}
