#ifndef I_CAMERA_SERVICE_H
#define I_CAMERA_SERVICE_H

#include <string>
#include <vector>
#include <memory>

namespace service {

struct PhotoResult {
    bool success;
    std::string message;
    std::string filePath;
    long long timestamp;
};

enum class PhotoState {
    IDLE,
    CAPTURING,
    PROCESSING,
    ERROR
};

struct PhotoStatus {
    PhotoState state;
    int progress; // 0-100
};

enum class RecordState {
    IDLE,
    RECORDING,
    STOPPING,
    ERROR
};

struct RecordStatus {
    RecordState state;
    int duration; // seconds
    std::string filePath;
};

class ICameraService {
public:
    virtual ~ICameraService() = default;

    // --- 拍照业务 ---
    // 返回: 0 成功, 非0 错误码
    virtual int takePhoto(int channel, bool save, const std::string& format, int quality, PhotoResult& result) = 0;
    virtual int startBurstPhoto(int count, int interval, const std::string& jobId) = 0;
    virtual PhotoStatus getPhotoStatus() = 0;

    // --- 录像业务 ---
    virtual int startRecord(int channel, int duration, bool audio, const std::string& recordId) = 0;
    virtual int stopRecord() = 0;
    virtual RecordStatus getRecordStatus() = 0;

    // --- 属性管理 ---
    virtual int setProperty(const std::string& key, const std::string& value) = 0;
    virtual std::string getProperty(const std::string& key) = 0;
    virtual std::string getAllPropertiesJson() = 0; // 或者返回对象结构

    // --- 文件/数据库 ---
    virtual std::string getMediaDatabasePath() = 0;
    virtual std::string getThumbnailDatabasePath() = 0;
    
    // --- 系统 ---
    virtual int factoryReset() = 0;
};

} // namespace service

#endif // I_CAMERA_SERVICE_H
