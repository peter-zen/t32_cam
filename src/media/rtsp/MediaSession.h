/**
 * @file MediaSession.h
 * @brief 媒体会话：统一生产者线程与 MediaFIFO 的封装
 */

#ifndef MEDIA_SESSION_H
#define MEDIA_SESSION_H

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include "../base/IMediaSource.h"
#include "../fifo/MediaFIFO.h"

namespace media {

class MediaSession {
public:
    MediaSession(std::shared_ptr<IMediaSource> source, size_t fifoSize = 30);
    ~MediaSession();

    bool start();
    bool stop();
    bool isRunning() const;
    bool requestIDR();

    MediaParams getParams() const;

    static int pullFrame(void** data, size_t* size, uint64_t* ts, void* user_data);
    static int releaseFrame(void** data, size_t* size, uint64_t* ts, void* user_data);

private:
    void producerLoop();
    int pullDataInternal(void** data, size_t* size, uint64_t* timestamp);
    int releaseDataInternal(void** data, size_t* size, uint64_t* timestamp);

    std::shared_ptr<IMediaSource> source_;
    std::shared_ptr<MediaFIFO<uint8_t>> fifo_;
    std::thread producerThread_;
    std::atomic<bool> running_{false};
    size_t fifoSize_;
    std::atomic<uint64_t> frameCount_{0};
    std::atomic<uint64_t> consumedCount_{0};
    std::atomic<uint64_t> pullTimeoutCount_{0};
    std::atomic<int> popTimeoutMs_{5};
    uint64_t startTimeUs_;
    uint64_t stopTimeUs_;
    std::string sessionType_;  // "VIDEO" or "AUDIO"
};

} // namespace media

#endif // MEDIA_SESSION_H
