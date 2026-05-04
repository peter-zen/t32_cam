#ifndef CAMERA_SERVICE_T32_H
#define CAMERA_SERVICE_T32_H

#include "../ICameraService.h"
#include "../../../media/snap/ImageSnap.h"
#include "../../../media/video/VideoRecorder.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <string>
#include <thread>

namespace service {

class CameraServiceT32 : public ICameraService {
public:
    CameraServiceT32();
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
    std::string getMediaDatabasePath() override;
    std::string getThumbnailDatabasePath() override;
    std::string getMediaList(int offset, int limit) override;
    int deleteFile(const std::string& filePath) override;
    
    // --- 系统 ---
    int factoryReset() override;

private:
    std::atomic<bool> is_recording_{false};
    std::atomic<bool> is_capturing_{false};
    std::mutex status_mutex_;
    std::mutex op_mutex_;
    std::shared_ptr<media::ImageSnap> image_snap_;
    std::shared_ptr<media::VideoRecorder> video_recorder_;
    std::string current_record_file_;
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
    std::thread timer_thread_;
    bool timer_stop_requested_ = false;
    TimerPhotoStatus timer_status_;
};

} // namespace service

#endif // CAMERA_SERVICE_T32_H
