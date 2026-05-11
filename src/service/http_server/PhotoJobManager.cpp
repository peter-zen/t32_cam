#include "PhotoJobManager.h"

#include "../../common/Common.h"
#include "TcpEventService.h"
#include "../../common/misc/Misc.h"

#include <json/json.h>
#include <elog.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <sstream>
#include <thread>
#include <unordered_map>

#define TAG "PhotoJob"

namespace service {

namespace {

constexpr size_t kMaxRetainedJobs = 64;

long long nowSeconds() {
    return static_cast<long long>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

template<typename T>
std::string numberToString(T value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string makeJobId(uint64_t sequence) {
    return "photo_job_" + numberToString(nowSeconds()) + "_" + numberToString(sequence);
}

Json::Value buildPhotoJson(const PhotoResult& result) {
    Json::Value photo(Json::objectValue);
    photo["photo_id"] = "photo_" + numberToString(result.timestamp);
    photo["filename"] = Misc::getFilename(result.filePath);
    photo["filepath"] = result.filePath;
    photo["size"] = static_cast<Json::UInt64>(Misc::getFileSize(result.filePath));
    photo["timestamp"] = static_cast<Json::Int64>(result.timestamp);
    return photo;
}

} // namespace

struct PhotoJobManager::Impl {
    struct StoredJob {
        PhotoJobRequest request;
        PhotoJobSnapshot snapshot;
    };

    mutable std::mutex mutex;
    std::condition_variable cv;
    std::thread worker;
    std::shared_ptr<ICameraService> cameraService;
    std::unordered_map<std::string, std::shared_ptr<StoredJob> > jobs;
    std::deque<std::string> pendingJobIds;
    std::deque<std::string> jobOrder;
    std::string lastJobId;
    bool stopRequested = false;
    std::atomic<uint64_t> nextJobSequence{0};

    void ensureWorkerLocked() {
        if (worker.joinable()) {
            return;
        }
        stopRequested = false;
        worker = std::thread(&Impl::workerLoop, this);
    }

    void pruneLocked() {
        while (jobOrder.size() > kMaxRetainedJobs) {
            const std::string& jobId = jobOrder.front();
            auto it = jobs.find(jobId);
            if (it == jobs.end()) {
                jobOrder.pop_front();
                continue;
            }

            const PhotoJobState state = it->second->snapshot.state;
            if (state == PhotoJobState::ACCEPTED || state == PhotoJobState::PROCESSING) {
                break;
            }

            jobs.erase(it);
            jobOrder.pop_front();
        }
    }

    void cancelPendingLocked() {
        while (!pendingJobIds.empty()) {
            const std::string jobId = pendingJobIds.front();
            pendingJobIds.pop_front();
            auto it = jobs.find(jobId);
            if (it == jobs.end()) {
                continue;
            }
            it->second->snapshot.state = PhotoJobState::FAILED;
            it->second->snapshot.progress = 100;
            it->second->snapshot.updatedAt = nowSeconds();
            it->second->snapshot.errorMessage = "server stopping";
            it->second->snapshot.resultCode = -1;
        }
    }

    void publishFinishedEvent(const StoredJob& job) {
        TcpEventMessage message;
        message.category = "camera";
        message.data["job_id"] = job.snapshot.jobId;
        message.data["client_request_id"] = job.snapshot.clientRequestId;
        message.data["status"] = photoJobStateToString(job.snapshot.state);

        if (job.snapshot.state == PhotoJobState::COMPLETED) {
            message.type = "camera.photo.completed";
            message.level = TcpEventLevel::INFO;
            message.data["photo"] = buildPhotoJson(job.snapshot.result);
        } else {
            message.type = "camera.photo.failed";
            message.level = TcpEventLevel::ERROR;
            message.data["error_message"] = job.snapshot.errorMessage;
        }

        TcpEventService::getInstance()->publish(message);
    }

    void workerLoop() {
        while (true) {
            std::shared_ptr<StoredJob> job;
            std::shared_ptr<ICameraService> activeCameraService;

            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this]() { return stopRequested || !pendingJobIds.empty(); });
                if (stopRequested && pendingJobIds.empty()) {
                    break;
                }

                if (pendingJobIds.empty()) {
                    continue;
                }

                const std::string jobId = pendingJobIds.front();
                pendingJobIds.pop_front();

                auto it = jobs.find(jobId);
                if (it == jobs.end()) {
                    continue;
                }

                job = it->second;
                activeCameraService = cameraService;
                job->snapshot.state = PhotoJobState::PROCESSING;
                job->snapshot.progress = 50;
                job->snapshot.updatedAt = nowSeconds();
            }

            PhotoResult result;
            int ret = -1;
            if (activeCameraService) {
                ret = activeCameraService->takePhoto(job->request.channel,
                                                    job->request.save,
                                                    job->request.format,
                                                    job->request.quality,
                                                    result);
            } else {
                result.message = "camera service unavailable";
            }

            StoredJob finishedJob;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = jobs.find(job->snapshot.jobId);
                if (it == jobs.end()) {
                    continue;
                }

                if (ret == 0 && result.success) {
                    it->second->snapshot.state = PhotoJobState::COMPLETED;
                    it->second->snapshot.progress = 100;
                    it->second->snapshot.updatedAt = nowSeconds();
                    it->second->snapshot.result = result;
                    it->second->snapshot.resultCode = 0;
                    it->second->snapshot.errorMessage.clear();
                } else {
                    it->second->snapshot.state = PhotoJobState::FAILED;
                    it->second->snapshot.progress = 100;
                    it->second->snapshot.updatedAt = nowSeconds();
                    it->second->snapshot.result = result;
                    it->second->snapshot.resultCode = ret;
                    it->second->snapshot.errorMessage =
                        result.message.empty() ? "capture failed" : result.message;
                }

                finishedJob = *it->second;
                pruneLocked();
            }

            publishFinishedEvent(finishedJob);
        }
    }
};

PhotoJobManager& PhotoJobManager::getInstance() {
    static PhotoJobManager instance;
    return instance;
}

PhotoJobManager::PhotoJobManager()
    : impl_(new Impl()) {
}

PhotoJobManager::~PhotoJobManager() {
    stop();
}

bool PhotoJobManager::submit(const std::shared_ptr<ICameraService>& cameraService,
                             const PhotoJobRequest& request,
                             PhotoJobSnapshot& snapshot) {
    if (!cameraService) {
        return false;
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->cameraService = cameraService;
    impl_->ensureWorkerLocked();

    std::shared_ptr<Impl::StoredJob> job(new Impl::StoredJob());
    job->request = request;
    job->snapshot.jobId = makeJobId(impl_->nextJobSequence.fetch_add(1) + 1);
    job->snapshot.clientRequestId = request.clientRequestId;
    job->snapshot.state = PhotoJobState::ACCEPTED;
    job->snapshot.progress = 0;
    job->snapshot.submittedAt = nowSeconds();
    job->snapshot.updatedAt = job->snapshot.submittedAt;

    impl_->jobs[job->snapshot.jobId] = job;
    impl_->pendingJobIds.push_back(job->snapshot.jobId);
    impl_->jobOrder.push_back(job->snapshot.jobId);
    impl_->lastJobId = job->snapshot.jobId;
    impl_->pruneLocked();

    snapshot = job->snapshot;
    impl_->cv.notify_one();
    return true;
}

bool PhotoJobManager::getSnapshot(const std::string& jobId, PhotoJobSnapshot& snapshot) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto it = impl_->jobs.find(jobId);
    if (it == impl_->jobs.end()) {
        return false;
    }
    snapshot = it->second->snapshot;
    return true;
}

bool PhotoJobManager::getLatestSnapshot(PhotoJobSnapshot& snapshot) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->lastJobId.empty()) {
        return false;
    }

    auto it = impl_->jobs.find(impl_->lastJobId);
    if (it == impl_->jobs.end()) {
        return false;
    }

    snapshot = it->second->snapshot;
    return true;
}

void PhotoJobManager::stop() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->stopRequested = true;
        impl_->cancelPendingLocked();
    }
    impl_->cv.notify_all();

    if (impl_->worker.joinable()) {
        impl_->worker.join();
    }
}

const char* photoJobStateToString(PhotoJobState state) {
    switch (state) {
        case PhotoJobState::PROCESSING:
            return "processing";
        case PhotoJobState::COMPLETED:
            return "completed";
        case PhotoJobState::FAILED:
            return "failed";
        case PhotoJobState::ACCEPTED:
        default:
            return "accepted";
    }
}

} // namespace service
