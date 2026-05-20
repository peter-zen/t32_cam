#include "CameraServiceT32.h"
#include "../CameraPropertyService.h"

#ifndef SIMULATION_MODE

#include <elog.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdio.h>
#include <json/json.h>
#include "../../../storage/MetadataDao.h"
#include "../../../common/misc/Misc.h"
#include "../../../common/Common.h"
#include "../../../config/setting/Settings.h"
#include "../../../hal/ingenic/sensor-config.h"
#include "../../../storage/MetadataDao.h"
#include <sys/statvfs.h>

#define TAG "CamT32"

// Max resolution supported by hardware JPEG encoder (sensor native resolution)
static constexpr int HW_ENCODER_MAX_W = 2560;
static constexpr int HW_ENCODER_MAX_H = 1440;

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
    CameraPropertyService& props = CameraPropertyService::getInstance();
    props.getVideoRecordConfig(width, height, fps, bitrateKbps);

    videoParams->setResolution(width, height);
    videoParams->setFrameRate(fps);
    videoParams->setBitrate(bitrateKbps);

    // Convert registry codec value (1=H.264, 2=H.265) to VideoCodecFormat enum (0=H264, 1=H265)
    int codec = props.getVideoRecordCodec();
    videoParams->setCodecFormat(codec == 2 ? media::VideoCodecFormat::H265 : media::VideoCodecFormat::H264);

    // Convert registry rcMode value to VideoRcMode enum
    int rcModeVal = props.getVideoRecordRcMode();
    media::VideoRcMode rcMode;
    switch (rcModeVal) {
        case 2: rcMode = media::VideoRcMode::VBR; break;
        case 3: rcMode = media::VideoRcMode::CVBR; break;
        case 4: rcMode = media::VideoRcMode::SMART; break;
        default: rcMode = media::VideoRcMode::CBR; break;
    }
    videoParams->setRcMode(rcMode);
}

const char* sensorTypeName() {
#if defined(SENSOR_TYPE_SC4336P)
    return "sc4336p";
#elif defined(SENSOR_TYPE_GC4653)
    return "gc4653";
#else
    return "gc5613";
#endif
}

void logDefaultStreamProfiles() {
    elog_i(TAG,
           "stream default profile: sensor=%s stream0={enable=%d size=%dx%d fps=%d/%d} stream1={enable=%d size=%dx%d fps=%d/%d}",
           sensorTypeName(),
           CHN0_EN,
           FIRST_SENSOR_WIDTH,
           FIRST_SENSOR_HEIGHT,
           FIRST_SENSOR_FRAME_RATE_NUM,
           FIRST_SENSOR_FRAME_RATE_DEN,
           CHN1_EN,
           FIRST_SENSOR_WIDTH_SECOND,
           FIRST_SENSOR_HEIGHT_SECOND,
           FIRST_SENSOR_FRAME_RATE_NUM,
           FIRST_SENSOR_FRAME_RATE_DEN);
}

const char* videoCodecName(media::VideoCodecFormat format) {
    return format == media::VideoCodecFormat::H265 ? "H265" : "H264";
}

const char* videoRcModeName(media::VideoRcMode mode) {
    switch (mode) {
        case media::VideoRcMode::VBR: return "VBR";
        case media::VideoRcMode::CVBR: return "CVBR";
        case media::VideoRcMode::AVBR: return "AVBR";
        case media::VideoRcMode::SMART: return "SMART";
        case media::VideoRcMode::FIXQP: return "FIXQP";
        case media::VideoRcMode::CBR:
        default:
            return "CBR";
    }
}

static bool isTestMode() {
    const char* testMode = std::getenv("HTC_TEST_MODE");
    return testMode != nullptr && std::string(testMode) == "1";
}

} // namespace

CameraServiceT32::CameraServiceT32() {
    elog_i(TAG, "CameraServiceT32 created");
    initScheduler();
}

CameraServiceT32::~CameraServiceT32() {
    stopScheduler();
    stopTimerPhoto(nullptr);
    elog_i(TAG, "CameraServiceT32 destroyed");
}

int CameraServiceT32::takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) {
    std::lock_guard<std::mutex> op_lock(op_mutex_);
    std::lock_guard<std::mutex> status_lock(status_mutex_);
    if (is_capturing_) {
        elog_w(TAG, "Already capturing");
        return -1;
    }

    is_capturing_ = true;

    // Test Mode: fixed sensor native resolution; Work Mode: follow Settings
    int width, height, jpegQuality;
    if (isTestMode()) {
        width = 2560;
        height = 1440;
        jpegQuality = (quality > 0 && quality <= 99) ? quality : 85;
        elog_i(TAG, "Test Mode takePhoto: fixed size=%dx%d", width, height);
    } else {
        int sizeIndex = Settings::getInstance()->stillSize;
        if (sizeIndex < 0 || sizeIndex >= SNAP_IMG_SIZE_MAX) sizeIndex = SNAP_IMG_SIZE_4M;
        width = SnapImgSize[sizeIndex].width;
        height = SnapImgSize[sizeIndex].height;
        jpegQuality = (quality > 0 && quality <= 99) ? quality : CameraPropertyService::getInstance().getStillQualityForJpeg();
    }

    elog_i(TAG, "Taking photo: ch=%d, save=%d, size=%dx%d, quality=%d", channel, save, width, height, jpegQuality);

    // Generate filename
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << "/mnt/sdcard/DCIM/IMG_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".jpg";
    std::string filename = oss.str();

    bool ok;
    if (width <= HW_ENCODER_MAX_W && height <= HW_ENCODER_MAX_H) {
        // Hardware path: within sensor resolution, use hardware JPEG encoder
        if (!image_snap_) {
            image_snap_ = std::make_shared<media::ImageSnap>();
        }
        media::ImageSnapParams params;
        params.setImageSize(width, height);
        image_snap_->setParams(params);
        ok = image_snap_->snap(filename);
    } else {
        // Software path: exceeds sensor resolution, use strip-based resize + software JPEG
        if (!large_snap_) {
            large_snap_ = std::make_shared<media::LargeImageSnap>();
        }
        ok = large_snap_->snapLarge(filename, width, height, jpegQuality);
    }

    if (ok) {
        result.success = true;
        result.filePath = filename;
        result.timestamp = (long long)now;
        is_capturing_ = false;
        return 0;
    } else {
        result.success = false;
        result.message = "Snap failed";
        is_capturing_ = false;
        return -1;
    }
}

int CameraServiceT32::startBurstPhoto(int count, int interval, const std::string& jobId) {
    (void)jobId;
    if (count <= 0) {
        elog_w(TAG, "Burst photo: invalid count=%d", count);
        return -1;
    }

    // Fallback to configured interval
    if (interval <= 0) {
        interval = static_cast<int>(Settings::getInstance()->shootingInterval) * 100;
    }
    if (interval < 100) interval = 100;

    {
        std::lock_guard<std::mutex> lock(burst_mutex_);
        if (burst_thread_.joinable()) {
            elog_w(TAG, "Burst photo already running");
            return -1;
        }
    }

    if (burst_thread_.joinable()) {
        burst_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(burst_mutex_);
        burst_stop_requested_ = false;
        burst_completed_ = 0;
        burst_total_ = count;
    }

    burst_thread_ = std::thread([this, count, interval]() {
        for (int i = 0; i < count; i++) {
            {
                std::lock_guard<std::mutex> lock(burst_mutex_);
                if (burst_stop_requested_) break;
            }

            PhotoResult result;
            if (takePhoto(0, true, "jpg", 85, result) == 0) {
                std::lock_guard<std::mutex> lock(burst_mutex_);
                burst_completed_++;
            }

            if (i < count - 1) {
                std::this_thread::sleep_for(std::chrono::milliseconds(interval));
            }
        }
    });

    elog_i(TAG, "Burst photo started: count=%d, interval=%dms", count, interval);
    return 0;
}

PhotoStatus CameraServiceT32::getPhotoStatus() {
    PhotoStatus status;
    status.state = is_capturing_ ? PhotoState::CAPTURING : PhotoState::IDLE;
    status.progress = is_capturing_ ? 50 : 0;
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

    const std::string base_dir = "/mnt/sdcard/.preview/";
    if (!Misc::createDirectory(base_dir)) {
        return -1;
    }

    const std::string path = build_capture_path(base_dir);
    media::ImageSnapParams params;
    params.setImageSize(width, height);
    if (!image_snap_) {
        image_snap_ = std::make_shared<media::ImageSnap>();
    }
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
    if (is_recording_) {
        elog_w(TAG, "Already recording");
        return -1;
    }

    is_recording_ = true;

    // Generate filename
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    /* Temporary: record to /tmp to verify if SD card is the fps bottleneck */
    oss << "/tmp/VID_" << std::put_time(&tm, "%Y%m%d_%H%M%S") << ".mp4";
    std::string filename = oss.str();
    current_record_file_ = filename;

    auto vidParam = std::make_shared<media::VideoParams>();
    if (isTestMode()) {
        vidParam->setResolution(2560, 1440);
        vidParam->setFrameRate(30);
        vidParam->setBitrate(4000);
        vidParam->setCodecFormat(media::VideoCodecFormat::H265);
        vidParam->setRcMode(media::VideoRcMode::CBR);
        vidParam->setGop(60);
        elog_i(TAG, "Test Mode record: fixed 2560x1440 H265 30fps");
    } else {
        applyConfiguredVideoParams(vidParam);
    }
    std::shared_ptr<media::AudioParams> audParam = nullptr;
    if (audio) {
        audParam = std::make_shared<media::AudioParams>();
        audParam->setDeviceType(media::AudioDeviceType::AUDIO_IN);
        audParam->setDeviceId(1);
        audParam->setChannelId(0);
        audParam->setVolume(Settings::getInstance()->audioRecordVolume);
        audParam->setGain(Settings::getInstance()->audioRecordGain);
        audParam->setCodecFormat(media::AudioCodecFormat::AAC);
        audParam->setSampleRate(media::AudioSampleRate::SR_16000);
        audParam->setChannelCount(1);
    }
    video_recorder_ = std::make_shared<media::VideoRecorder>(vidParam, audParam);

    // If duration not specified by API, read from settings
    if (duration <= 0) {
        duration = CameraPropertyService::getInstance().getVideoRecordLength();
    }

    logDefaultStreamProfiles();
    int recordStreamId = 0;
    const char* recordStreamEnv = std::getenv("HTC_RECORD_STREAM_ID");
    if (recordStreamEnv && strcmp(recordStreamEnv, "1") == 0) {
        recordStreamId = 1;
    }
    int width = 0;
    int height = 0;
    vidParam->getResolution(width, height);
    elog_i(TAG,
           "record config: file=%s sensor=0 stream=%d size=%dx%d fps=%d bitrate=%dKbps codec=%s rc=%s duration=%d audio=%d",
           current_record_file_.c_str(),
           recordStreamId,
           width,
           height,
           vidParam->getFrameRate(),
           vidParam->getBitrate(),
           videoCodecName(vidParam->getCodecFormat()),
           videoRcModeName(vidParam->getRcMode()),
           duration,
           audio ? 1 : 0);

    // Disk space check and loop recording cleanup
    {
        int bitrateKbps = vidParam->getBitrate();
        // Estimate space needed (Kbps * 1000 / 8) + 10% overhead, in MB
        long long estimatedMB = 0;
        if (duration > 0) {
            estimatedMB = static_cast<long long>(bitrateKbps) * 1000 * duration / 8 / (1024 * 1024);
        }
        estimatedMB = estimatedMB * 11 / 10 + 10; // +10% overhead + 10MB safety

        struct statvfs stat;
        if (statvfs("/mnt/sdcard", &stat) == 0) {
            unsigned long long blockSize = stat.f_frsize ? stat.f_frsize : stat.f_bsize;
            long long freeMB = static_cast<long long>((stat.f_bavail * blockSize) >> 20);

            if (freeMB < estimatedMB) {
                bool autoCover = Settings::getInstance()->autoCover != 0;
                if (autoCover) {
                    // Delete oldest video(s) to make space
                    MetadataDao dao;
                    int attempts = 0;
                    while (freeMB < estimatedMB && attempts < 50) {
                        std::string oldest = dao.getOldestMediaPath(2); // type=2: video
                        if (oldest.empty()) break;

                        // Delete from DB and filesystem
                        dao.deleteMedia(oldest);
                        remove(oldest.c_str());
                        elog_i(TAG, "Loop recording: deleted %s to free space", oldest.c_str());
                        attempts++;

                        // Re-read free space
                        if (statvfs("/mnt/sdcard", &stat) == 0) {
                            freeMB = static_cast<long long>((stat.f_bavail * blockSize) >> 20);
                        }
                    }
                    if (freeMB < estimatedMB) {
                        elog_w(TAG, "Insufficient space even after cleanup: need=%lld MB, free=%lld MB", estimatedMB, freeMB);
                        is_recording_ = false;
                        return -1;
                    }
                } else {
                    elog_w(TAG, "Insufficient space: need=%lld MB, free=%lld MB", estimatedMB, freeMB);
                    is_recording_ = false;
                    return -1;
                }
            }
        }
    }

    elog_i(TAG, "Start recording: duration=%d", duration);
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

int CameraServiceT32::stopRecord() {
    std::lock_guard<std::mutex> lock(op_mutex_);
    if (!is_recording_) {
        return 0;
    }
    if (video_recorder_) {
        video_recorder_->stopRecorder();
    }
    is_recording_ = false;
    elog_i(TAG, "Stop recording");
    return 0;
}

RecordStatus CameraServiceT32::getRecordStatus() {
    RecordStatus status;
    status.state = is_recording_ ? RecordState::RECORDING : RecordState::IDLE;
    status.duration = 0;
    status.filePath = current_record_file_;
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
    return "/mnt/sdcard/data/db/media_file.db";
}

std::string CameraServiceT32::getThumbnailDatabasePath() {
    return "/mnt/sdcard/data/db/media_thumb.db";
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

void CameraServiceT32::initScheduler() {
    Settings* settings = Settings::getInstance().get();
    if (settings->timerEn == 0) {
        elog_i(TAG, "Scheduler: timerEn=0, skipping init");
        return;
    }

    std::lock_guard<std::mutex> lock(scheduler_mutex_);
    if (scheduler_thread_.joinable()) {
        elog_w(TAG, "Scheduler already running");
        return;
    }

    scheduler_stop_requested_ = false;
    scheduler_thread_ = std::thread([this]() {
        elog_i(TAG, "Scheduler thread started");
        while (true) {
            {
                std::unique_lock<std::mutex> lock(scheduler_mutex_);
                if (scheduler_cv_.wait_for(lock, std::chrono::seconds(30),
                    [this]() { return scheduler_stop_requested_; })) {
                    break;
                }
            }

            if (!isInTimeWindow()) continue;

            // CAM_Mode guard: scheduler auto-trigger respects cameraMode
            uint8_t camMode = Settings::getInstance()->cameraMode;
            if (camMode == 2) {  // Mode 2: Video only, skip scheduled photo
                elog_w(TAG, "Scheduler: skipped photo, cameraMode=%d (video only)", camMode);
                continue;
            }

            // In time window: take a photo
            PhotoResult result;
            if (takePhoto(0, true, "jpg", 85, result) == 0) {
                elog_i(TAG, "Scheduler: photo taken -> %s", result.filePath.c_str());
            }

            // Wait for Timer_Interval_Time before next shot within this window
            Settings* settings = Settings::getInstance().get();
            int lapseSeconds = static_cast<int>(settings->timerLapse_m) * 60 +
                               static_cast<int>(settings->timerLapse_s);
            if (lapseSeconds < 1) lapseSeconds = 60;

            {
                std::unique_lock<std::mutex> lock(scheduler_mutex_);
                if (scheduler_cv_.wait_for(lock, std::chrono::seconds(lapseSeconds),
                    [this]() { return scheduler_stop_requested_; })) {
                    break;
                }
            }
        }
        elog_i(TAG, "Scheduler thread stopped");
    });
}

void CameraServiceT32::stopScheduler() {
    {
        std::lock_guard<std::mutex> lock(scheduler_mutex_);
        scheduler_stop_requested_ = true;
    }
    scheduler_cv_.notify_all();
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
}

bool CameraServiceT32::isInTimeWindow() {
    Settings* settings = Settings::getInstance().get();

    // Check weekRepeats: 7-bit bitmap, bit0=Sunday, bit6=Saturday
    time_t now = time(nullptr);
    struct tm* tm_now = localtime(&now);
    int wday = tm_now->tm_wday; // 0=Sunday
    int dayMask = 1 << wday;
    if ((static_cast<int>(settings->weekRepeats) & dayMask) == 0) {
        return false;
    }

    // Check time windows (timer1s/e, timer2s/e, timer3s/e)
    int nowMinutes = tm_now->tm_hour * 60 + tm_now->tm_min;

    auto inWindow = [&](uint8_t startH, uint8_t startM, uint8_t endH, uint8_t endM) -> bool {
        int startMin = static_cast<int>(startH) * 60 + static_cast<int>(startM);
        int endMin = static_cast<int>(endH) * 60 + static_cast<int>(endM);
        if (startMin == endMin) return false; // disabled/zero window
        if (startMin < endMin) {
            return nowMinutes >= startMin && nowMinutes < endMin;
        }
        // Overnight window (e.g. 22:00 - 06:00)
        return nowMinutes >= startMin || nowMinutes < endMin;
    };

    if (inWindow(settings->timer1s_h, settings->timer1s_m, settings->timer1e_h, settings->timer1e_m)) return true;
    if (inWindow(settings->timer2s_h, settings->timer2s_m, settings->timer2e_h, settings->timer2e_m)) return true;
    if (inWindow(settings->timer3s_h, settings->timer3s_m, settings->timer3e_h, settings->timer3e_m)) return true;

    return false;
}

} // namespace service

#endif // SIMULATION_MODE
