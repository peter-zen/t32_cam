#include "CameraServiceT32.h"

#ifndef SIMULATION_MODE

#include <elog.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

#define TAG "CamT32"

namespace service {

CameraServiceT32::CameraServiceT32() {
    elog_i(TAG, "CameraServiceT32 created");
    image_snap_ = std::make_shared<media::ImageSnap>();
    video_recorder_ = std::make_shared<media::VideoRecorder>();
}

CameraServiceT32::~CameraServiceT32() {
    elog_i(TAG, "CameraServiceT32 destroyed");
}

int CameraServiceT32::takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) {
    std::lock_guard<std::mutex> lock(op_mutex_);
    elog_i(TAG, "Taking photo: ch=%d, save=%d", channel, save);
    
    // Generate filename
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << "/sdcard/DCIM/IMG_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".jpg";
    std::string filename = oss.str();

    media::ImageSnapParams params;
    // params.setImageSize(1920, 1080); // Default
    image_snap_->setParams(params);

    if (image_snap_->snap(filename)) {
        result.success = true;
        result.filePath = filename;
        result.timestamp = (long long)now;
        return 0;
    } else {
        result.success = false;
        result.message = "Snap failed";
        return -1;
    }
}

int CameraServiceT32::startBurstPhoto(int count, int interval, const std::string& jobId) {
    elog_e(TAG, "Burst photo not implemented yet");
    return -1;
}

PhotoStatus CameraServiceT32::getPhotoStatus() {
    PhotoStatus status;
    status.state = PhotoState::IDLE; // TODO: Track state
    status.progress = 0;
    return status;
}

int CameraServiceT32::startRecord(int channel, int duration, bool audio, const std::string& recordId) {
    std::lock_guard<std::mutex> lock(op_mutex_);
    elog_i(TAG, "Start record: duration=%d", duration);

    // Generate filename
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << "/sdcard/DCIM/VID_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".mp4";
    std::string filename = oss.str();

    auto vidParam = std::make_shared<media::VideoParams>();
    vidParam->setResolution(1920, 1080);
    vidParam->setFrameRate(30);
    vidParam->setBitrate(4000000);
    auto audParam = std::make_shared<media::AudioParams>();
    audParam->setDeviceType(media::AudioDeviceType::AUDIO_IN);
    audParam->setDeviceId(1);
    audParam->setChannelId(0);
    audParam->setVolume(80);
    audParam->setGain(28);
    audParam->setCodecFormat(media::AudioCodecFormat::AAC);
    audParam->setSampleRate(media::AudioSampleRate::SR_16000);
    audParam->setChannelCount(1);
    video_recorder_ = std::make_shared<media::VideoRecorder>(vidParam, audParam);

    if (video_recorder_->record(filename, duration)) {
        return 0;
    } else {
        return -1;
    }
}

int CameraServiceT32::stopRecord() {
    std::lock_guard<std::mutex> lock(op_mutex_);
    elog_i(TAG, "Stop record");
    if (video_recorder_->stopRecorder()) {
        return 0;
    }
    return -1;
}

RecordStatus CameraServiceT32::getRecordStatus() {
    RecordStatus status;
    status.state = RecordState::IDLE; // TODO: Track state
    return status;
}

int CameraServiceT32::setProperty(const std::string& key, const std::string& value) {
    elog_i(TAG, "Set property: %s=%s", key.c_str(), value.c_str());
    // TODO: Map to IMP ISP settings
    return 0;
}

std::string CameraServiceT32::getProperty(const std::string& key) {
    return "";
}

std::string CameraServiceT32::getAllPropertiesJson() {
    return "{}";
}

std::string CameraServiceT32::getMediaDatabasePath() {
    return "/sdcard/data/db/media.db";
}

std::string CameraServiceT32::getThumbnailDatabasePath() {
    return "/sdcard/data/db/thumb.db";
}

int CameraServiceT32::factoryReset() {
    elog_i(TAG, "Factory reset");
    // Remove db, reset config
    return 0;
}

} // namespace service

#endif // SIMULATION_MODE
