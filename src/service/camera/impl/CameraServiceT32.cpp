#include "CameraServiceT32.h"
#include "../CameraPropertyService.h"

#ifndef SIMULATION_MODE

#include <elog.h>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdio.h>
#include <json/json.h>
#include "../../../storage/MetadataDao.h"
#include "../../../common/misc/Misc.h"

#define TAG "CamT32"

namespace service {

namespace {

std::string build_capture_path(const std::string& base_dir) {
    std::ostringstream oss;
    oss << base_dir
        << "preview_"
        << std::chrono::steady_clock::now().time_since_epoch().count()
        << "_"
        << std::hash<std::thread::id>{}(std::this_thread::get_id())
        << ".jpg";
    return oss.str();
}

bool read_binary_file(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    data.resize(static_cast<size_t>(size));
    return file.read(reinterpret_cast<char*>(data.data()), size).good();
}

void applyConfiguredVideoParams(const std::shared_ptr<media::VideoParams>& videoParams) {
    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrateKbps = 4096;
    CameraPropertyService::getInstance().getVideoRecordConfig(width, height, fps, bitrateKbps);

    videoParams->setResolution(width, height);
    videoParams->setFrameRate(fps);
    videoParams->setBitrate(bitrateKbps * 1024);
}

} // namespace

CameraServiceT32::CameraServiceT32() {
    elog_i(TAG, "CameraServiceT32 created");
    image_snap_ = std::make_shared<media::ImageSnap>();
    video_recorder_ = std::make_shared<media::VideoRecorder>();
}

CameraServiceT32::~CameraServiceT32() {
    stopTimerPhoto(nullptr);
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

int CameraServiceT32::startTimerPhoto(int channel, int intervalMs, int totalCount, const std::string& jobId) {
    if (intervalMs <= 0 || totalCount < 0) {
        return -1;
    }

    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        if (timer_status_.running) {
            return -1;
        }
    }

    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_stop_requested_ = false;
        timer_status_.running = true;
        timer_status_.jobId = jobId;
        timer_status_.intervalMs = intervalMs;
        timer_status_.totalCount = totalCount;
        timer_status_.completedCount = 0;
        timer_status_.channel = channel;
    }

    timer_thread_ = std::thread([this]() {
        while (true) {
            int channel = 0;
            int interval_ms = 0;
            bool should_stop = false;
            bool reached_limit = false;
            {
                std::unique_lock<std::mutex> lock(timer_mutex_);
                reached_limit = timer_status_.totalCount > 0 &&
                                timer_status_.completedCount >= timer_status_.totalCount;
                should_stop = timer_stop_requested_ || reached_limit;
                if (!should_stop) {
                    interval_ms = timer_status_.intervalMs;
                    channel = timer_status_.channel;
                    should_stop = timer_cv_.wait_for(
                        lock,
                        std::chrono::milliseconds(interval_ms),
                        [this]() { return timer_stop_requested_; });
                }
            }

            if (should_stop || reached_limit) {
                break;
            }

            PhotoResult result;
            if (takePhoto(channel, true, "jpg", 85, result) == 0) {
                std::lock_guard<std::mutex> lock(timer_mutex_);
                timer_status_.completedCount++;
            }
        }

        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_status_.running = false;
        timer_stop_requested_ = false;
    });

    return 0;
}

int CameraServiceT32::stopTimerPhoto(TimerPhotoStatus* status) {
    {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timer_stop_requested_ = true;
        timer_status_.running = false;
    }
    timer_cv_.notify_all();

    if (timer_thread_.joinable()) {
        timer_thread_.join();
    }

    if (status) {
        *status = getTimerPhotoStatus();
    }
    return 0;
}

TimerPhotoStatus CameraServiceT32::getTimerPhotoStatus() {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    return timer_status_;
}

int CameraServiceT32::capturePreviewFrame(int channel, int width, int height, std::vector<uint8_t>& data) {
    (void)channel;
    if (width <= 0) {
        width = 1920;
    }
    if (height <= 0) {
        height = 1080;
    }

    std::lock_guard<std::mutex> lock(op_mutex_);

    const std::string base_dir = "/sdcard/.preview/";
    if (!Misc::createDirectory(base_dir)) {
        return -1;
    }

    const std::string path = build_capture_path(base_dir);
    media::ImageSnapParams params;
    params.setImageSize(width, height);
    image_snap_->setParams(params);

    if (!image_snap_->snap(path)) {
        remove(path.c_str());
        return -1;
    }

    bool ok = read_binary_file(path, data);
    remove(path.c_str());
    return ok ? 0 : -1;
}

int CameraServiceT32::startRecord(int channel, int duration, bool audio, const std::string& recordId) {
    (void)channel;
    (void)recordId;
    std::lock_guard<std::mutex> lock(op_mutex_);
    elog_i(TAG, "Start record: duration=%d", duration);

    // Generate filename
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << "/sdcard/DCIM/VID_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".mp4";
    std::string filename = oss.str();

    auto vidParam = std::make_shared<media::VideoParams>();
    applyConfiguredVideoParams(vidParam);
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
        audParam->setChannelCount(1);
    }
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
    Json::Value propertyJson;
    std::string error;
    int ret = CameraPropertyService::getInstance().setPropertyValue(key, Json::Value(value), &propertyJson, &error);
    if (ret != 0) {
        elog_e(TAG, "Set property failed: %s=%s, error=%s", key.c_str(), value.c_str(), error.c_str());
        return ret;
    }

    elog_i(TAG, "Set property: %s=%s", key.c_str(), value.c_str());
    return 0;
}

std::string CameraServiceT32::getProperty(const std::string& key) {
    std::string value;
    std::string error;
    if (CameraPropertyService::getInstance().getPropertyValueString(key, value, &error) != 0) {
        elog_w(TAG, "Get property failed: %s, error=%s", key.c_str(), error.c_str());
        return "";
    }
    return value;
}

std::string CameraServiceT32::getAllPropertiesJson() {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, CameraPropertyService::getInstance().getAllPropertiesJson());
}

std::string CameraServiceT32::getMediaDatabasePath() {
    return "/sdcard/data/db/media_file.db";
}

std::string CameraServiceT32::getThumbnailDatabasePath() {
    return "/sdcard/data/db/media_thumb.db";
}

std::string CameraServiceT32::getMediaList(int offset, int limit) {
    MetadataDao dao;
    std::vector<MediaItem> items = dao.getTimeline(offset, limit);

    Json::Value list(Json::arrayValue);
    for (const auto& item : items) {
        Json::Value jItem;
        jItem["id"] = item.id;
        jItem["type"] = item.type;
        jItem["path"] = item.filePath;
        jItem["size"] = static_cast<Json::UInt64>(item.fileSize);
        jItem["timestamp"] = static_cast<Json::UInt64>(item.timestamp);
        jItem["duration"] = item.duration;
        jItem["width"] = item.width;
        jItem["height"] = item.height;
        list.append(jItem);
    }

    Json::FastWriter writer;
    return writer.write(list);
}

int CameraServiceT32::deleteFile(const std::string& filePath) {
    MetadataDao dao;
    if (dao.deleteMedia(filePath)) {
        remove(filePath.c_str());
        return 0;
    }
    return -1;
}

int CameraServiceT32::factoryReset() {
    elog_i(TAG, "Factory reset");
    // Remove db, reset config
    return 0;
}

} // namespace service

#endif // SIMULATION_MODE
