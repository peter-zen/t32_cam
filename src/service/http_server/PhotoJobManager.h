#ifndef PHOTO_JOB_MANAGER_H
#define PHOTO_JOB_MANAGER_H

#include "ICameraService.h"

#include <memory>
#include <mutex>
#include <string>

namespace service {

struct PhotoJobRequest {
    int channel = 0;
    bool save = true;
    std::string format = "jpg";
    int quality = 85;
    std::string clientRequestId;
};

enum class PhotoJobState {
    ACCEPTED,
    PROCESSING,
    COMPLETED,
    FAILED
};

struct PhotoJobSnapshot {
    std::string jobId;
    std::string clientRequestId;
    PhotoJobState state = PhotoJobState::ACCEPTED;
    int progress = 0;
    long long submittedAt = 0;
    long long updatedAt = 0;
    PhotoResult result;
    std::string errorMessage;
    int resultCode = 0;
};

class PhotoJobManager {
public:
    static PhotoJobManager& getInstance();

    ~PhotoJobManager();

    bool submit(const std::shared_ptr<ICameraService>& cameraService,
                const PhotoJobRequest& request,
                PhotoJobSnapshot& snapshot);
    bool getSnapshot(const std::string& jobId, PhotoJobSnapshot& snapshot) const;
    bool getLatestSnapshot(PhotoJobSnapshot& snapshot) const;
    void stop();

private:
    PhotoJobManager();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

const char* photoJobStateToString(PhotoJobState state);

} // namespace service

#endif // PHOTO_JOB_MANAGER_H
