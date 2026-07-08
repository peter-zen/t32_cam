#ifndef CAMERA_SERVICE_T32_H
#define CAMERA_SERVICE_T32_H

#include "../CameraRecorder.h"
#include "../ICameraService.h"
#include "../../../media/snap/ImageSnap.h"
#include "../../../media/snap/LargeImageSnap.h"
#include <atomic>
#include <condition_variable>
#include <ctime>
#include <mutex>
#include <memory>
#include <string>
#include <thread>

namespace storage { class StoragePaths; }

namespace service {

class CameraServiceT32 : public ICameraService {
public:
    CameraServiceT32(std::shared_ptr<storage::StoragePaths> storage);
    ~CameraServiceT32() override;

    // --- 拍照业务 ---
    int takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) override;
    int startBurstPhoto(int count, int interval, const std::string& jobId) override;
    PhotoStatus getPhotoStatus() override;
    int startTimerPhoto(int channel, int intervalMs, int totalCount, const std::string& jobId) override;
    int stopTimerPhoto(TimerPhotoStatus* status) override;
    TimerPhotoStatus getTimerPhotoStatus() override;
    int capturePreviewFrame(int channel, int width, int height, std::vector<uint8_t>& data) override;

    // --- 录像业务 ---
    int startRecord(int channel, int duration, bool audio, const std::string& recordId) override;
    int stopRecord() override;
    RecordStatus getRecordStatus() override;

    // --- 属性管理 ---
    int setProperty(const std::string& key, const std::string& value) override;
    std::string getProperty(const std::string& key) override;
    std::string getAllPropertiesJson() override;

    // --- 文件/数据库 ---
    std::string getMediaRoot() override;
    std::string getMediaDatabasePath() override;
    std::string getThumbnailDatabasePath() override;
    std::string getMediaList(int offset, int limit) override;
    int deleteFile(const std::string& filePath) override;
    
    // --- 系统 ---
    int factoryReset() override;
    void prewarm() override;
    void initScheduler();
    void stopScheduler();

private:
    bool isInTimeWindow();
    std::atomic<bool> is_recording_{false};
    std::atomic<bool> is_capturing_{false};
    std::mutex status_mutex_;
    std::mutex op_mutex_;
    std::shared_ptr<media::ImageSnap> image_snap_;
    std::shared_ptr<media::LargeImageSnap> large_snap_;
    std::shared_ptr<service::camera::CameraRecorder> video_recorder_;
    std::string current_record_file_;
    time_t last_photo_sec_ = 0;     // 同秒拍照序号（op_mutex_ 锁内，修同秒撞名）
    int photo_seq_in_sec_ = 0;
    std::shared_ptr<storage::StoragePaths> storage_;  // S3 注入：媒体/db 路径唯一来源
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
    std::thread timer_thread_;
    bool timer_stop_requested_ = false;
    TimerPhotoStatus timer_status_;
    std::mutex burst_mutex_;
    std::thread burst_thread_;
    bool burst_stop_requested_ = false;
    int burst_completed_ = 0;
    int burst_total_ = 0;
    std::thread scheduler_thread_;
    bool scheduler_stop_requested_ = false;
    std::mutex scheduler_mutex_;
    std::condition_variable scheduler_cv_;
};

} // namespace service

#endif // CAMERA_SERVICE_T32_H
