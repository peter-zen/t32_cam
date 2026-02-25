#ifndef CAMERA_SERVICE_SIM_H
#define CAMERA_SERVICE_SIM_H

#include "../ICameraService.h"
#include "../../../media/snap/ImageSnap.h"
#include "../../../media/video/VideoRecorder.h"
#include <atomic>
#include <mutex>
#include <memory>
#include <string>

namespace service {

class CameraServiceSim : public ICameraService {
public:
    CameraServiceSim();
    ~CameraServiceSim() override;

    // --- 拍照业务 ---
    int takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) override;
    int startBurstPhoto(int count, int interval, const std::string& jobId) override;
    PhotoStatus getPhotoStatus() override;

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
};

} // namespace service

#endif // CAMERA_SERVICE_SIM_H
