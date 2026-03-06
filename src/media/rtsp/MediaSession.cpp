#include "MediaSession.h"
#include <elog.h>
#include <chrono>
#include <cstring>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "MED_SESSION"

namespace media {

MediaSession::MediaSession(std::shared_ptr<IMediaSource> source, size_t fifoSize)
    : source_(source)
    , fifo_(std::make_shared<MediaFIFO<uint8_t>>(fifoSize))
    , fifoSize_(fifoSize)
{
}

MediaSession::~MediaSession()
{
    stop();
}

bool MediaSession::start()
{
    if (running_) {
        return true;
    }

    if (!source_ || !fifo_) {
        elog_e(LOG_TAG, "Source or FIFO not initialized");
        return false;
    }

    MediaParams params = source_->getParams();
    sessionType_ = (params.type == MediaType::VIDEO) ? "VIDEO" : "AUDIO";

    if (params.type == MediaType::AUDIO) {
        int frameMs = params.audioFrameDurationUs > 0 ? (params.audioFrameDurationUs / 1000) : 40;
        int timeoutMs = std::max(5, std::min(20, frameMs / 2));
        popTimeoutMs_.store(timeoutMs);
    } else {
        int fps = params.videoFrameRate > 0 ? params.videoFrameRate : 15;
        int frameMs = std::max(1, 1000 / fps);
        int timeoutMs = std::max(3, std::min(12, frameMs / 2));
        popTimeoutMs_.store(timeoutMs);
    }

    if (!source_->open()) {
        elog_e(LOG_TAG, "Failed to open source");
        return false;
    }

    frameCount_ = 0;
    consumedCount_ = 0;
    pullTimeoutCount_ = 0;
    fifo_->resetStats();

    running_ = true;
    startTimeUs_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    producerThread_ = std::thread(&MediaSession::producerLoop, this);

    elog_i(LOG_TAG, "[%s] MediaSession started. FIFO size: %zu",
            sessionType_.c_str(), fifoSize_);
    elog_i(LOG_TAG, "[%s] Consumer pop timeout=%d ms",
           sessionType_.c_str(), popTimeoutMs_.load());

    return true;
}

bool MediaSession::stop()
{
    if (!running_) {
        return true;
    }

    running_ = false;
    stopTimeUs_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    if (producerThread_.joinable()) {
        producerThread_.join();
    }

    if (source_) {
        source_->close();
    }

    uint64_t produced = frameCount_.load();
    uint64_t consumed = consumedCount_.load();
    uint64_t pullTimeouts = pullTimeoutCount_.load();
    size_t fifoDrops = fifo_->getDropCount();
    uint64_t durationUs = stopTimeUs_ - startTimeUs_;
    double durationSec = durationUs / 1000000.0;
    double avgProducedRate = (durationSec > 0) ? (produced / durationSec) : 0.0;
    double avgConsumedRate = (durationSec > 0) ? (consumed / durationSec) : 0.0;

    const char* rateUnit = (sessionType_ == "VIDEO") ? "fps" : "pps";

    elog_i(LOG_TAG,
           "[%s] MediaSession stopped. produced=%llu consumed=%llu pull_timeout=%llu fifo_drop=%zu duration=%.3f sec avg_produced=%.2f %s avg_consumed=%.2f %s",
           sessionType_.c_str(),
           (unsigned long long)produced,
           (unsigned long long)consumed,
           (unsigned long long)pullTimeouts,
           fifoDrops,
           durationSec,
           avgProducedRate, rateUnit,
           avgConsumedRate, rateUnit);

    return true;
}

bool MediaSession::isRunning() const
{
    return running_;
}

bool MediaSession::requestIDR()
{
    if (source_) {
        return source_->requestIDR();
    }
    return false;
}

MediaParams MediaSession::getParams() const
{
    if (source_) {
        return source_->getParams();
    }
    return MediaParams();
}

int MediaSession::pullFrame(void** data, size_t* size, uint64_t* ts, void* user_data)
{
    MediaSession* session = static_cast<MediaSession*>(user_data);

    if (!session) {
        return -1;
    }

    return session->pullDataInternal(data, size, ts);
}

int MediaSession::releaseFrame(void** data, size_t* size, uint64_t* ts, void* user_data)
{
    MediaSession* session = static_cast<MediaSession*>(user_data);

    if (!session) {
        return -1;
    }

    return session->releaseDataInternal(data, size, ts);
}

void MediaSession::producerLoop()
{
    MediaParams params = source_->getParams();
    bool isVideo = (params.type == MediaType::VIDEO);
    std::chrono::microseconds pacingInterval(0);

    elog_i(LOG_TAG, "[%s] ProducerLoop started", sessionType_.c_str());

    if (isVideo) {
        int fps = params.videoFrameRate;
        if (fps > 0) {
            pacingInterval = std::chrono::microseconds(1000000 / fps);
            elog_i(LOG_TAG, "[%s] Producer pacing by fps=%d (%lld us)",
                   sessionType_.c_str(), fps, (long long)pacingInterval.count());
        }
    } else if (params.audioFrameDurationUs > 0) {
        pacingInterval = std::chrono::microseconds(params.audioFrameDurationUs);
        elog_i(LOG_TAG, "[%s] Producer pacing by audioFrameDurationUs=%d",
               sessionType_.c_str(), params.audioFrameDurationUs);
    }

    auto pushToSessionFifo = [this](void* data, size_t size, uint64_t timestamp, bool isKeyFrame) -> bool {
        if (fifo_->push(data, size, timestamp, isKeyFrame)) {
            frameCount_++;
            return true;
        }
        return false;
    };

    auto nextFrameTime = std::chrono::steady_clock::now() + pacingInterval;
    bool droppingGop = false;
    uint64_t lastFifoFullLogMs = 0;

    auto lastStatTime = std::chrono::steady_clock::now();
    uint64_t lastProduced = frameCount_.load();
    uint64_t lastConsumed = consumedCount_.load();
    uint64_t lastPullTimeout = pullTimeoutCount_.load();
    size_t lastFifoDrops = fifo_->getDropCount();

    while (running_) {
        void* data = nullptr;
        size_t size = 0;
        uint64_t timestamp = 0;

        int ret = source_->pullData(&data, &size, &timestamp);
        if (ret != 0) {
            if (ret == -2) {
                elog_i(LOG_TAG, "Source EOF reached, stopping producer");
                running_ = false;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        bool isKeyFrame = false;
        if (isVideo && data && size > 4) {
            uint8_t nalType = static_cast<uint8_t*>(data)[4] & 0x1F;
            if (nalType == 5 || nalType == 7 || nalType == 8) {
                isKeyFrame = true;
            }
        } else {
            isKeyFrame = true;
        }

        if (isVideo) {
            if (droppingGop) {
                if (isKeyFrame) {
                    droppingGop = false;
                    if (!pushToSessionFifo(data, size, timestamp, isKeyFrame)) {
                        if (fifo_->dropFirstNonKeyFrame()) {
                            elog_w(LOG_TAG, "[%s] FIFO full(%zu/%zu), dropped oldest P-frame for incoming I-frame",
                                   sessionType_.c_str(), fifo_->size(), fifoSize_);
                            if (!pushToSessionFifo(data, size, timestamp, isKeyFrame)) {
                                elog_w(LOG_TAG, "[%s] FIFO still full(%zu/%zu) after drop, forcing head drop",
                                       sessionType_.c_str(), fifo_->size(), fifoSize_);
                                fifo_->dropOldest();
                                pushToSessionFifo(data, size, timestamp, isKeyFrame);
                            }
                        } else {
                            elog_w(LOG_TAG, "[%s] FIFO full(%zu/%zu) of key frames, dropped oldest I-frame",
                                   sessionType_.c_str(), fifo_->size(), fifoSize_);
                            fifo_->dropOldest();
                            pushToSessionFifo(data, size, timestamp, isKeyFrame);
                        }
                    }
                } else {
                    source_->releaseData(&data, &size, &timestamp);
                }
            } else {
                if (!pushToSessionFifo(data, size, timestamp, isKeyFrame)) {
                    if (!isKeyFrame) {
                        elog_w(LOG_TAG, "[%s] FIFO full(%zu/%zu), dropping incoming P-frame and starting GOP drop",
                               sessionType_.c_str(), fifo_->size(), fifoSize_);
                        droppingGop = true;
                        source_->releaseData(&data, &size, &timestamp);
                    } else {
                        if (fifo_->dropFirstNonKeyFrame()) {
                            elog_w(LOG_TAG, "[%s] FIFO full(%zu/%zu), dropped oldest P-frame for incoming key frame",
                                   sessionType_.c_str(), fifo_->size(), fifoSize_);
                            pushToSessionFifo(data, size, timestamp, isKeyFrame);
                        } else {
                            elog_w(LOG_TAG, "[%s] FIFO full(%zu/%zu) of key frames, dropping oldest I-frame",
                                   sessionType_.c_str(), fifo_->size(), fifoSize_);
                            fifo_->dropOldest();
                            pushToSessionFifo(data, size, timestamp, isKeyFrame);
                        }
                    }
                }
            }
        } else {
            if (!pushToSessionFifo(data, size, timestamp, isKeyFrame)) {
                uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (nowMs - lastFifoFullLogMs > 1000) {
                    elog_w(LOG_TAG,
                           "[%s] FIFO full(%zu/%zu), dropping new frame. fifo_drops=%zu, produced=%llu, consumed=%llu, pull_timeout=%llu",
                           sessionType_.c_str(),
                           fifo_->size(), fifoSize_,
                           fifo_->getDropCount(),
                           (unsigned long long)frameCount_.load(),
                           (unsigned long long)consumedCount_.load(),
                           (unsigned long long)pullTimeoutCount_.load());
                    lastFifoFullLogMs = nowMs;
                }
                source_->releaseData(&data, &size, &timestamp);
            }
        }

        if (data) {
            source_->releaseData(&data, &size, &timestamp);
        }

        if (pacingInterval.count() > 0) {
            auto now = std::chrono::steady_clock::now();
            if (now > nextFrameTime + pacingInterval) {
                nextFrameTime = now + pacingInterval;
            } else {
                std::this_thread::sleep_until(nextFrameTime);
                nextFrameTime += pacingInterval;
            }
        }

        auto statNow = std::chrono::steady_clock::now();
        if (statNow - lastStatTime >= std::chrono::seconds(1)) {
            uint64_t produced = frameCount_.load();
            uint64_t consumed = consumedCount_.load();
            uint64_t pullTimeouts = pullTimeoutCount_.load();
            size_t fifoDrops = fifo_->getDropCount();
            elog_i(LOG_TAG,
                   "[%s] Stats: produced=%llu/s consumed=%llu/s pull_timeout=%llu/s fifo=%zu/%zu fifo_drops=%zu(+%zu)",
                   sessionType_.c_str(),
                   (unsigned long long)(produced - lastProduced),
                   (unsigned long long)(consumed - lastConsumed),
                   (unsigned long long)(pullTimeouts - lastPullTimeout),
                   fifo_->size(), fifoSize_,
                   fifoDrops, fifoDrops - lastFifoDrops);

            lastProduced = produced;
            lastConsumed = consumed;
            lastPullTimeout = pullTimeouts;
            lastFifoDrops = fifoDrops;
            lastStatTime = statNow;
        }
    }
}

int MediaSession::pullDataInternal(void** data, size_t* size, uint64_t* timestamp)
{
    MediaFIFO<uint8_t>::Frame frame;
    const int timeoutMs = std::max(1, popTimeoutMs_.load());
    if (!fifo_->popBlocking(frame, std::chrono::milliseconds(timeoutMs))) {
        pullTimeoutCount_++;
        return -1;
    }

    *data = frame.data;
    *size = frame.size;
    if (timestamp) {
        *timestamp = frame.timestamp_us;
    }
    consumedCount_++;

    return 0;
}

int MediaSession::releaseDataInternal(void** data, size_t* size, uint64_t* timestamp)
{
    MediaFIFO<uint8_t>::Frame frame;
    frame.data = *data;
    frame.size = *size;
    if (timestamp) {
        frame.timestamp_us = *timestamp;
    }

    fifo_->release(frame);

    *data = nullptr;
    *size = 0;

    return 0;
}

} // namespace media
