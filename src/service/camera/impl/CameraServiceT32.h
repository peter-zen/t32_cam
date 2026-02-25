#ifndef CAMERA_SERVICE_T32_H
#define CAMERA_SERVICE_T32_H

#include "../ICameraService.h"
#include "../../../media/snap/ImageSnap.h"
#include "../../../media/video/VideoRecorder.h"
#include <mutex>
#include <memory>

namespace service {

class CameraServiceT32 : public ICameraService {
public:
    CameraServiceT32();
    ~CameraServiceT32() override;

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
    std::shared_ptr<media::ImageSnap> image_snap_;
    std::shared_ptr<media::VideoRecorder> video_recorder_;
    std::mutex op_mutex_;
};

} // namespace service

#endif // CAMERA_SERVICE_T32_H
