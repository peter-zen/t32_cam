#include "MediaSession.h"
#include <elog.h>
#include <chrono>
#include <cstring>

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

    if (!source_->open()) {
        elog_e(LOG_TAG, "Failed to open source");
        return false;
    }

    running_ = true;
    startTimeUs_ = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    producerThread_ = std::thread(&MediaSession::producerLoop, this);

    elog_i(LOG_TAG, "[%s] MediaSession started. FIFO size: %zu",
            sessionType_.c_str(), fifoSize_);

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

    uint64_t frames = frameCount_.load();
    uint64_t durationUs = stopTimeUs_ - startTimeUs_;
    double durationSec = durationUs / 1000000.0;
    double avgRate = (durationSec > 0) ? (frames / durationSec) : 0.0;

    const char* rateUnit = (sessionType_ == "VIDEO") ? "fps" : "pps";

    elog_i(LOG_TAG, "[%s] MediaSession stopped. Frames produced: %lu, duration: %.3f sec, avg rate: %.2f %s",
            sessionType_.c_str(), (unsigned long)frames, durationSec, avgRate, rateUnit);

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
    std::chrono::microseconds frameInterval(0);

    elog_i(LOG_TAG, "[%s] ProducerLoop started", sessionType_.c_str());

    if (isVideo) {
        int fps = params.videoFrameRate;
        if (fps > 0) {
            frameInterval = std::chrono::microseconds(1000000 / fps);
        }
    }

    auto nextFrameTime = std::chrono::steady_clock::now() + frameInterval;
    bool droppingGop = false;

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

        if (frameInterval.count() > 0) {
            if (isVideo && droppingGop) {
                if (isKeyFrame) {
                    droppingGop = false;
                    if (!fifo_->push(data, size, timestamp, isKeyFrame)) {
                        if (fifo_->dropFirstNonKeyFrame()) {
                            elog_w(LOG_TAG, "FIFO full, dropped oldest P-frame for new I-frame");
                            if (!fifo_->push(data, size, timestamp, isKeyFrame)) {
                                elog_w(LOG_TAG, "FIFO still full after drop, forcing head drop");
                                fifo_->dropOldest();
                                fifo_->push(data, size, timestamp, isKeyFrame);
                            }
                        } else {
                            elog_w(LOG_TAG, "FIFO full of KeyFrames, dropped oldest I-frame for new I-frame");
                            fifo_->dropOldest();
                            fifo_->push(data, size, timestamp, isKeyFrame);
                        }
                    }
                } else {
                    source_->releaseData(&data, &size, &timestamp);
                }
            } else {
                if (!fifo_->push(data, size, timestamp, isKeyFrame)) {
                    if (isVideo && !isKeyFrame) {
                        elog_w(LOG_TAG, "FIFO full, dropping incoming P-frame and starting GOP drop");
                        droppingGop = true;
                        source_->releaseData(&data, &size, &timestamp);
                    } else {
                        if (fifo_->dropFirstNonKeyFrame()) {
                            elog_w(LOG_TAG, "FIFO full, dropped oldest P-frame for new KeyFrame");
                            fifo_->push(data, size, timestamp, isKeyFrame);
                        } else {
                            elog_w(LOG_TAG, "FIFO full of KeyFrames, dropping oldest I-frame for new KeyFrame");
                            fifo_->dropOldest();
                            fifo_->push(data, size, timestamp, isKeyFrame);
                        }
                    }
                }
            }

            if (frameInterval.count() > 0) {
                auto now = std::chrono::steady_clock::now();
                if (now > nextFrameTime + frameInterval) {
                    nextFrameTime = now + frameInterval;
                } else {
                    std::this_thread::sleep_until(nextFrameTime);
                    nextFrameTime += frameInterval;
                }
            }
        } else {
            if (fifo_->push(data, size, timestamp, isKeyFrame)) {
                frameCount_++;
            } else {
                static uint64_t lastLog = 0;
                uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (now - lastLog > 1000) {
                    elog_w(LOG_TAG, "FIFO full, dropping new frame (live source)");
                    lastLog = now;
                }
                source_->releaseData(&data, &size, &timestamp);
            }
        }
    }
}

int MediaSession::pullDataInternal(void** data, size_t* size, uint64_t* timestamp)
{
    MediaFIFO<uint8_t>::Frame frame;
    if (!fifo_->popBlocking(frame, std::chrono::milliseconds(5))) {
        return -1;
    }

    *data = frame.data;
    *size = frame.size;
    if (timestamp) {
        *timestamp = frame.timestamp_us;
    }

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
