#include "CameraRecorder.h"

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <elog.h>
#include <sstream>

#include "CameraPropertyService.h"
#include "../../config/setting/Settings.h"
#include "../../storage/MetadataDao.h"

namespace service {
namespace camera {

namespace {

constexpr const char* kTag = "CamRec";

constexpr int kGop = 60;
constexpr int kAudioDeviceId = 1;
constexpr int kAudioChannelId = 0;
constexpr int kAudioChannels = 1;

constexpr int kMaxAutoCoverAttempts = 50;
constexpr double kDiskEstimateOverhead = 1.1;
constexpr int kDiskSafetyMB = 10;

} // namespace

CameraRecorder::CameraRecorder() = default;

CameraRecorder::~CameraRecorder() {
    // 不主动 stop：caller 显式管理生命周期。
    // 若 caller 忘记 stop，~VideoRecorder 会做收尾。
}

std::string CameraRecorder::computeThumbnailPath(const std::string& filePath) {
    // 规则: <video dir>/thumb/<basename>.jpg
    // 例: /sdcard/DCIM/VID_20260607_101010.mp4 → /sdcard/DCIM/thumb/VID_20260607_101010.jpg
    // 项目是 C++14 + GCC 5.4,没有 std::filesystem,用字符串操作代替。
    size_t slash = filePath.find_last_of('/');
    std::string dir = (slash == std::string::npos) ? std::string() : filePath.substr(0, slash);
    std::string base = (slash == std::string::npos) ? filePath : filePath.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    std::string stem = (dot == std::string::npos) ? base : base.substr(0, dot);
    std::string thumbDir = dir + "/thumb";
    return thumbDir + "/" + stem + ".jpg";
}

std::shared_ptr<media::VideoParams> CameraRecorder::buildVideoParams(int bitrateKbpsOverride) {
    auto& cps = CameraPropertyService::getInstance();
    int width = 0, height = 0, fps = 0, bitrateKbps = 0;
    cps.getVideoRecordConfig(width, height, fps, bitrateKbps);
    if (width <= 0 || height <= 0) { width = 1920; height = 1080; }
    if (fps <= 0) fps = 30;
    if (bitrateKbps <= 0) bitrateKbps = 4000;
    if (bitrateKbpsOverride > 0) {
        elog_i(kTag, "buildVideoParams: bitrate override %d kbps (CPS value was %d kbps)",
               bitrateKbpsOverride, bitrateKbps);
        bitrateKbps = bitrateKbpsOverride;
    }

    int codecReg = cps.getVideoRecordCodec();
    int rcReg = cps.getVideoRecordRcMode();

    auto vp = std::make_shared<media::VideoParams>();
    vp->setResolution(width, height);
    vp->setFrameRate(fps);
    vp->setBitrate(bitrateKbps);
    vp->setCodecFormat(codecReg == 2 ? media::VideoCodecFormat::H265 : media::VideoCodecFormat::H264);
    switch (rcReg) {
        case 2:  vp->setRcMode(media::VideoRcMode::VBR); break;
        case 3:  vp->setRcMode(media::VideoRcMode::CVBR); break;
        case 4:  vp->setRcMode(media::VideoRcMode::SMART); break;
        case 1:
        default: vp->setRcMode(media::VideoRcMode::CBR); break;
    }
    vp->setGop(kGop);
    return vp;
}

std::shared_ptr<media::AudioParams> CameraRecorder::buildAudioParams() {
    auto settings = Settings::getInstance();
    auto ap = std::make_shared<media::AudioParams>();
    ap->setDeviceType(media::AudioDeviceType::AUDIO_IN);
    ap->setDeviceId(kAudioDeviceId);
    ap->setChannelId(kAudioChannelId);
    ap->setVolume(settings->audioRecordVolume);
    ap->setGain(settings->audioRecordGain);
    ap->setCodecFormat(media::AudioCodecFormat::AAC);
    ap->setSampleRate(media::AudioSampleRate::SR_16000);
    ap->setChannelCount(kAudioChannels);
    return ap;
}

bool CameraRecorder::ensureDiskSpace(int bitrateKbps, int durationSec, bool autoCover,
                                     std::string& errorMessage) {
    long long estimatedMB = 0;
    if (bitrateKbps > 0 && durationSec > 0) {
        estimatedMB = static_cast<long long>(bitrateKbps) * 1000 * durationSec / 8 / (1024 * 1024);
    }
    estimatedMB = static_cast<long long>(estimatedMB * kDiskEstimateOverhead) + kDiskSafetyMB;

    struct statvfs stat;
    if (statvfs("/mnt/sdcard", &stat) != 0) {
        std::ostringstream oss;
        oss << "statvfs /mnt/sdcard failed: " << strerror(errno);
        errorMessage = oss.str();
        return false;
    }
    unsigned long long blockSize = stat.f_frsize ? stat.f_frsize : stat.f_bsize;
    long long freeMB = static_cast<long long>((stat.f_bavail * blockSize) >> 20);

    if (freeMB >= estimatedMB) {
        return true;
    }

    if (!autoCover) {
        std::ostringstream oss;
        oss << "insufficient disk space: need=" << estimatedMB
            << "MB, free=" << freeMB << "MB, autoCover=0";
        errorMessage = oss.str();
        return false;
    }

    int attempts = 0;
    while (freeMB < estimatedMB && attempts < kMaxAutoCoverAttempts) {
        std::string oldest;
        try {
            MetadataDao dao;
            oldest = dao.getOldestMediaPath(2);  // type=2: video
        } catch (...) {
            errorMessage = "MetadataDao getOldestMediaPath threw";
            return false;
        }
        if (oldest.empty()) break;

        try {
            MetadataDao dao;
            dao.deleteMedia(oldest);
            if (unlink(oldest.c_str()) == 0) {
                elog_i(kTag, "autoCover: deleted %s to free space", oldest.c_str());
            } else {
                elog_w(kTag, "autoCover: unlink %s failed: %s",
                       oldest.c_str(), strerror(errno));
            }
        } catch (...) {
            errorMessage = "autoCover: deleteMedia/unlink threw";
            return false;
        }
        attempts++;

        if (statvfs("/mnt/sdcard", &stat) != 0) {
            errorMessage = "statvfs re-check failed";
            return false;
        }
        freeMB = static_cast<long long>((stat.f_bavail * blockSize) >> 20);
    }

    if (freeMB < estimatedMB) {
        std::ostringstream oss;
        oss << "insufficient disk space even after autoCover: need=" << estimatedMB
            << "MB, free=" << freeMB << "MB, attempts=" << attempts;
        errorMessage = oss.str();
        return false;
    }
    return true;
}

bool CameraRecorder::record(const std::string& filePath, int durationSec,
                            const RecordOptions& options) {
    if (is_recording_.load()) {
        elog_w(kTag, "record: already recording, reject new request");
        return false;
    }
    if (!options.onComplete) {
        elog_w(kTag, "record: onComplete callback not set, reject");
        return false;
    }

    bool autoCover = options.autoCover;
    if (!options.autoCover && Settings::getInstance()->autoCover != 0) {
        autoCover = true;
    }

    int effectiveDuration = durationSec;
    if (effectiveDuration <= 0) {
        effectiveDuration = CameraPropertyService::getInstance().getVideoRecordLength();
        if (effectiveDuration <= 0) {
            effectiveDuration = 10;
        }
    }

    auto vidParam = buildVideoParams(options.bitrateKbpsOverride);
    std::shared_ptr<media::AudioParams> audParam;
    if (options.audio) {
        audParam = buildAudioParams();
    }

    std::string diskError;
    if (!ensureDiskSpace(vidParam->getBitrate(), effectiveDuration, autoCover, diskError)) {
        elog_w(kTag, "record: %s", diskError.c_str());
        return false;
    }

    is_recording_.store(true);
    stop_requested_.store(false);
    start_time_ = std::chrono::steady_clock::now();
    current_duration_ms_.store(0);
    std::string thumbPath = computeThumbnailPath(filePath);
    auto onComplete = options.onComplete;

    try {
        // concurrentSnap 控制 CH2 缩略图抓取（processCmdVideoRecord 路径可通过 env 关闭）
        video_recorder_ = std::make_shared<media::VideoRecorder>(vidParam, audParam, options.concurrentSnap);
    } catch (const std::exception& e) {
        is_recording_.store(false);
        std::string msg = std::string("VideoRecorder ctor failed: ") + e.what();
        elog_e(kTag, "record: %s", msg.c_str());
        return false;
    } catch (...) {
        is_recording_.store(false);
        elog_e(kTag, "record: VideoRecorder ctor threw unknown");
        return false;
    }

    bool started = video_recorder_->record(
        filePath,
        [this, onComplete, filePath, thumbPath](bool /*success*/) {
            auto endTime = std::chrono::steady_clock::now();
            int64_t durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - start_time_).count();
            current_duration_ms_.store(durationMs);

            int64_t fileSize = 0;
            struct stat st;
            if (stat(filePath.c_str(), &st) == 0) {
                fileSize = static_cast<int64_t>(st.st_size);
            }

            bool manual = stop_requested_.load();
            RecordResult result;
            result.error = manual ? RecordError::UserStop : RecordError::None;
            result.filePath = filePath;
            result.thumbnailPath = (video_recorder_ && video_recorder_->hasThumbnail())
                                   ? thumbPath : std::string();
            result.durationMs = durationMs;
            result.fileSizeBytes = fileSize;
            result.stoppedManually = manual;
            result.errorMessage = manual ? "recording stopped by user via stop()" : std::string();

            is_recording_.store(false);
            elog_i(kTag, "record done: file=%s duration=%lldms size=%lld bytes manual=%d",
                   filePath.c_str(), (long long)durationMs, (long long)fileSize, manual ? 1 : 0);

            try {
                onComplete(result);
            } catch (const std::exception& e) {
                elog_e(kTag, "onComplete callback threw: %s", e.what());
            } catch (...) {
                elog_e(kTag, "onComplete callback threw unknown");
            }
        },
        effectiveDuration);

    if (!started) {
        is_recording_.store(false);
        video_recorder_.reset();
        elog_e(kTag, "record: VideoRecorder::record() start failed for %s", filePath.c_str());
        return false;
    }

    elog_i(kTag, "record started: file=%s duration=%ds autoCover=%d audio=%d",
           filePath.c_str(), effectiveDuration, autoCover ? 1 : 0, options.audio ? 1 : 0);
    return true;
}

bool CameraRecorder::stop() {
    if (!is_recording_.load()) {
        elog_w(kTag, "stop: not recording, no-op");
        return false;
    }
    stop_requested_.store(true);
    if (video_recorder_) {
        video_recorder_->stopRecorder();
    }
    elog_i(kTag, "stop: stop signal sent");
    return true;
}

int64_t CameraRecorder::getCurrentDurationMs() const {
    if (!is_recording_.load()) {
        return current_duration_ms_.load();
    }
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
}

const std::vector<uint8_t>& CameraRecorder::getThumbnailData() const {
    static const std::vector<uint8_t> kEmpty;
    if (!video_recorder_) return kEmpty;
    return video_recorder_->getThumbnailData();
}

bool CameraRecorder::hasThumbnail() const {
    return video_recorder_ && video_recorder_->hasThumbnail();
}

void CameraRecorder::releaseVideoResources() {
    // 重置 video_recorder_ shared_ptr → 引用计数归零 → 触发 ~VideoRecorder →
    // deinitialize() → uninitVideo() → video_->exit() → IMP_System_Exit()。
    // 这条调用链会立刻释放 SDK 的帧缓冲池，是避免 zram swap 尖峰的关键。
    if (video_recorder_) {
        elog_i(kTag, "releaseVideoResources: releasing SDK frame buffers now");
        video_recorder_.reset();
    }
    is_recording_.store(false);
    current_duration_ms_.store(0);
}

} // namespace camera
} // namespace service
