#include "CameraServiceSim.h"
#include <elog.h>
#include <thread>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <stdio.h>
#include "../../../common/misc/Misc.h"

#define TAG "CamSim"

namespace service {

CameraServiceSim::CameraServiceSim() {
    elog_i(TAG, "CameraServiceSim created");
    image_snap_ = std::make_shared<media::ImageSnap>();
}

CameraServiceSim::~CameraServiceSim() {
    elog_i(TAG, "CameraServiceSim destroyed");
}

int CameraServiceSim::takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (is_capturing_) {
        elog_w(TAG, "Already capturing");
        return -1;
    }
    is_capturing_ = true;

    elog_i(TAG, "Simulating photo capture: ch=%d, save=%d, fmt=%s", channel, save, format.c_str());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << "IMG_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".jpg";
    std::string filename = oss.str();
    std::string base = std::string("./sim_sdcard/DCIM/");
    std::string path = base + filename;

    if (save) {
        Misc::createDirectory(base);
        media::ImageSnapParams params;
        image_snap_->setParams(params);
        if (!image_snap_->snap(path)) {
            is_capturing_ = false;
            result.success = false;
            result.message = "Snap failed (Sim)";
            return -1;
        }
    }

    result.success = true;
    result.message = "Success (Sim)";
    result.filePath = path;
    result.timestamp = (long long)now;

    is_capturing_ = false;
    return 0;
}

int CameraServiceSim::startBurstPhoto(int count, int interval, const std::string& jobId) {
    elog_i(TAG, "Start burst photo (Sim): count=%d, interval=%d", count, interval);
    return 0;
}

PhotoStatus CameraServiceSim::getPhotoStatus() {
    PhotoStatus status;
    status.state = is_capturing_ ? PhotoState::CAPTURING : PhotoState::IDLE;
    status.progress = is_capturing_ ? 50 : 0;
    return status;
}

int CameraServiceSim::startRecord(int channel, int duration, bool audio, const std::string& recordId) {
    std::lock_guard<std::mutex> lock(op_mutex_);
    if (is_recording_) {
        elog_w(TAG, "Already recording");
        return -1;
    }
    is_recording_ = true;
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << "VID_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".mp4";
    std::string filename = oss.str();
    std::string base = std::string("./sim_sdcard/DCIM/");
    current_record_file_ = base + filename;
    Misc::createDirectory(base);

    auto vidParam = std::make_shared<media::VideoParams>();
    vidParam->setResolution(1920, 1080);
    vidParam->setFrameRate(15);
    vidParam->setBitrate(4000000);
    vidParam->setCodecFormat(media::VideoCodecFormat::H264);
    vidParam->setRcMode(media::VideoRcMode::CBR);

    std::shared_ptr<media::AudioParams> audParam = nullptr;
    if (audio) {
        audParam = std::make_shared<media::AudioParams>();
        audParam->setDeviceType(media::AudioDeviceType::AUDIO_IN);
        audParam->setDeviceId(1);
        audParam->setChannelId(0);
        audParam->setVolume(80);
        audParam->setGain(28);
        audParam->setCodecFormat(media::AudioCodecFormat::AAC);
        audParam->setSampleRate(media::AudioSampleRate::SR_16000);
        audParam->setSoundMode(media::AudioSoundMode::MONO);
        audParam->setChannelCount(1);
    }
    video_recorder_ = std::make_shared<media::VideoRecorder>(vidParam, audParam);
    elog_i(TAG, "Start recording (Sim): duration=%d", duration);
    bool ok = video_recorder_->record(current_record_file_, [this](bool) {
        std::lock_guard<std::mutex> l(op_mutex_);
        is_recording_ = false;
    }, duration);
    if (!ok) {
        is_recording_ = false;
        return -1;
    }
    return 0;
}

int CameraServiceSim::stopRecord() {
    std::lock_guard<std::mutex> lock(op_mutex_);
    if (!is_recording_) {
        return 0;
    }
    if (video_recorder_) {
        video_recorder_->stopRecorder();
    }
    is_recording_ = false;
    elog_i(TAG, "Stop recording (Sim)");
    return 0;
}

RecordStatus CameraServiceSim::getRecordStatus() {
    RecordStatus status;
    status.state = is_recording_ ? RecordState::RECORDING : RecordState::IDLE;
    status.duration = 0; // TODO: Calculate elapsed
    status.filePath = current_record_file_;
    return status;
}

int CameraServiceSim::setProperty(const std::string& key, const std::string& value) {
    elog_i(TAG, "Set property (Sim): %s = %s", key.c_str(), value.c_str());
    return 0;
}

std::string CameraServiceSim::getProperty(const std::string& key) {
    return "sim_value";
}

std::string CameraServiceSim::getAllPropertiesJson() {
    return "{\"sim\": true}";
}

std::string CameraServiceSim::getMediaDatabasePath() {
    return "sdcard/data/db/media.db";
}

std::string CameraServiceSim::getThumbnailDatabasePath() {
    return "sdcard/data/db/thumb.db";
}

int CameraServiceSim::factoryReset() {
    elog_i(TAG, "Factory reset (Sim)");
    return 0;
}

} // namespace service
