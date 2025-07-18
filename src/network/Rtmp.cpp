#include "Rtmp.h"
#include <thread>
#include <iostream>

using namespace network;

Rtmp::Rtmp(const std::string &url, int duration, std::function<void(int error_code)> callback)
    : url(url), duration(duration), callback(callback), rtmp_thread_run(false) {}

Rtmp::~Rtmp() {
    if (rtmp_thread && rtmp_thread->joinable()) {
        rtmp_thread_run = false;
        rtmp_thread->join();
    }
}

bool Rtmp::start() {
    if (rtmp_thread_run) {
        return false;
    }
    rtmp_thread_run = true;
    rtmp_thread = std::make_shared<std::thread>(&Rtmp::uploadFunction, this);
    return true;
}

bool Rtmp::stop() {
    if (!rtmp_thread_run) {
        return false;
    }
    rtmp_thread_run = false;
    if (rtmp_thread && rtmp_thread->joinable()) {
        rtmp_thread->join();
    }
    return true;
}

int Rtmp::rtmpUpload() {
    std::cout << "Starting RTMP upload to " << url << " for " << duration << " seconds." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(duration));
    std::cout << "RTMP upload completed." << std::endl;
    return 0;
}

void Rtmp::uploadFunction() {
    int error_code = rtmpUpload();
    if (callback) {
        callback(error_code);
    }
}