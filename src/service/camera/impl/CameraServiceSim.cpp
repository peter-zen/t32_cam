#include "CameraServiceSim.h"
#include "../CameraPropertyService.h"
#include <elog.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <functional>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <json/json.h>
#include "../../../storage/MetadataDao.h"
#include "../../../storage/StoragePaths.h"
#include "../../../common/misc/Misc.h"

#define TAG "CamSim"

namespace service {

namespace {

bool directoryExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool directoryWritable(const std::string& path) {
    return directoryExists(path) && access(path.c_str(), W_OK) == 0;
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty()) {
        return name;
    }
    if (!base.empty() && base.back() == '/') {
        return base + name;
    }
    return base + "/" + name;
}

std::string parentPath(const std::string& path) {
    if (path.empty()) {
        return "";
    }
    const std::string trimmed = (path.size() > 1 && path.back() == '/')
                                    ? path.substr(0, path.size() - 1)
                                    : path;
    const size_t pos = trimmed.find_last_of('/');
    if (pos == std::string::npos) {
        return "";
    }
    if (pos == 0) {
        return "/";
    }
    return trimmed.substr(0, pos);
}

std::string normalizePath(const std::string& path) {
    char resolved[PATH_MAX];
    if (realpath(path.c_str(), resolved) != nullptr) {
        return std::string(resolved);
    }
    return path;
}

std::string build_capture_path(const std::string& base_dir) {
    std::ostringstream oss;
    oss << "preview_"
        << std::chrono::steady_clock::now().time_since_epoch().count()
        << "_"
        << std::hash<std::thread::id>{}(std::this_thread::get_id())
        << ".jpg";
    return joinPath(base_dir, oss.str());
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
    videoParams->setBitrate(bitrateKbps);
}

} // namespace

CameraServiceSim::CameraServiceSim(std::shared_ptr<storage::StoragePaths> storage)
    : storage_(storage) {
    elog_i(TAG, "CameraServiceSim created");
    image_snap_ = std::make_shared<media::ImageSnap>();
}

CameraServiceSim::~CameraServiceSim() {
    stopTimerPhoto(nullptr);
    elog_i(TAG, "CameraServiceSim destroyed");
}

int CameraServiceSim::takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) {
    std::lock_guard<std::mutex> op_lock(op_mutex_);
    std::lock_guard<std::mutex> status_lock(status_mutex_);
    if (is_capturing_) {
        elog_w(TAG, "Already capturing");
        return -1;
    }
    is_capturing_ = true;

    elog_i(TAG, "Simulating photo capture: ch=%d, save=%d, fmt=%s", channel, save, format.c_str());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    auto now = std::time(nullptr);
    std::string filename = storage::StoragePaths::makeMediaName(storage::MediaKind::Image, now, 0);
    const std::string base = storage_->mediaRoot();
    const std::string path = joinPath(base, filename);

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
        if (Misc::getFileSize(path) == 0) {
            if (!Misc::copyFile("./src/hal/simu/res/sample_image.jpeg", path)) {
                is_capturing_ = false;
                result.success = false;
                result.message = "Snap fallback failed (Sim)";
                return -1;
            }
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

int CameraServiceSim::startTimerPhoto(int channel, int intervalMs, int totalCount, const std::string& jobId) {
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

int CameraServiceSim::stopTimerPhoto(TimerPhotoStatus* status) {
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

TimerPhotoStatus CameraServiceSim::getTimerPhotoStatus() {
    std::lock_guard<std::mutex> lock(timer_mutex_);
    return timer_status_;
}

int CameraServiceSim::capturePreviewFrame(int channel, int width, int height, std::vector<uint8_t>& data) {
    (void)channel;
    if (width <= 0) {
        width = 1920;
    }
    if (height <= 0) {
        height = 1080;
    }

    std::lock_guard<std::mutex> lock(op_mutex_);

    const std::string base_dir = storage_->previewDir();
    if (!Misc::createDirectory(base_dir)) {
        return -1;
    }

    const std::string path = build_capture_path(base_dir);
    media::ImageSnapParams params;
    params.setImageSize(width, height);
    image_snap_->setParams(params);

    bool snapped = image_snap_->snap(path);
    if ((!snapped || Misc::getFileSize(path) == 0) &&
        !Misc::copyFile("./src/hal/simu/res/sample_image.jpeg", path)) {
        remove(path.c_str());
        return -1;
    }

    bool ok = read_binary_file(path, data);
    remove(path.c_str());
    return ok ? 0 : -1;
}

int CameraServiceSim::startRecord(int channel, int duration, bool audio, const std::string& recordId) {
    (void)channel;
    (void)recordId;
    std::lock_guard<std::mutex> lock(op_mutex_);
    if (is_recording_) {
        elog_w(TAG, "Already recording");
        return -1;
    }
    is_recording_ = true;
    auto now = std::time(nullptr);
    std::string filename = storage::StoragePaths::makeMediaName(storage::MediaKind::Video, now, 0);
    const std::string base = storage_->mediaRoot();
    current_record_file_ = joinPath(base, filename);
    Misc::createDirectory(base);

    auto vidParam = std::make_shared<media::VideoParams>();
    applyConfiguredVideoParams(vidParam);
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
    Json::Value propertyJson;
    std::string error;
    int ret = CameraPropertyService::getInstance().setPropertyValue(key, Json::Value(value), &propertyJson, &error);
    if (ret != 0) {
        elog_e(TAG, "Set property failed (Sim): %s = %s, error=%s", key.c_str(), value.c_str(), error.c_str());
        return ret;
    }

    elog_i(TAG, "Set property (Sim): %s = %s", key.c_str(), value.c_str());
    return 0;
}

std::string CameraServiceSim::getProperty(const std::string& key) {
    std::string value;
    std::string error;
    if (CameraPropertyService::getInstance().getPropertyValueString(key, value, &error) != 0) {
        elog_w(TAG, "Get property failed (Sim): %s, error=%s", key.c_str(), error.c_str());
        return "";
    }
    return value;
}

std::string CameraServiceSim::getAllPropertiesJson() {
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return Json::writeString(writer, CameraPropertyService::getInstance().getAllPropertiesJson());
}

std::string CameraServiceSim::getMediaRoot() {
    return storage_->mediaRoot();
}

std::string CameraServiceSim::getMediaDatabasePath() {
    return storage_->mediaDb();
}

std::string CameraServiceSim::getThumbnailDatabasePath() {
    return storage_->thumbDb();
}

std::string CameraServiceSim::getMediaList(int offset, int limit) {
    Json::Value list(Json::arrayValue);
    MetadataDao dao;
    std::vector<MediaItem> items = dao.getTimeline(offset, limit);
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

    if (list.empty()) {
        Json::Value item;
        item["id"] = 1;
        item["type"] = 1;
        item["path"] = joinPath(storage_->mediaRoot(), "IMG_001.jpg");
        item["size"] = 0;
        item["timestamp"] = static_cast<Json::UInt64>(std::time(nullptr));
        item["duration"] = 0;
        item["width"] = 1920;
        item["height"] = 1080;
        list.append(item);
    }

    Json::FastWriter writer;
    return writer.write(list);
}

int CameraServiceSim::deleteFile(const std::string& filePath) {
    elog_i(TAG, "Delete file (Sim): %s", filePath.c_str());
    return Misc::deleteFile(filePath) ? 0 : -1;
}

int CameraServiceSim::factoryReset() {
    elog_i(TAG, "Factory reset (Sim)");
    return 0;
}

} // namespace service
