/**
 * @file MediaFIFO.h
 * @brief 统一的媒体FIFO缓冲区 - 支持视频和音频
 */

#ifndef MEDIA_FIFO_H
#define MEDIA_FIFO_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>

namespace media {

template<typename T>
class MediaFIFO {
public:
    struct Frame {
        void* data;
        size_t size;
        uint64_t timestamp_us;
        bool isKeyFrame;
    };
    
    explicit MediaFIFO(size_t capacity = 20);
    ~MediaFIFO();
    
    bool push(void* data, size_t size, uint64_t timestamp, bool isKeyFrame = false);
    bool pushBlocking(void* data, size_t size, uint64_t timestamp,
                   std::chrono::milliseconds timeout, bool isKeyFrame = false);
    bool pushOrDrop(void* data, size_t size, uint64_t timestamp, bool isKeyFrame = false);

    bool pop(Frame& frame);
    bool popBlocking(Frame& frame, std::chrono::milliseconds timeout);
    
    void release(const Frame& frame);
    
    size_t size() const;
    bool empty() const;
    bool full() const;
    bool frontIsKeyFrame() const;
    
    // Drop strategy methods
    bool dropOldest();
    bool dropFirstNonKeyFrame();
    
    size_t getDropCount() const;
    void resetStats();
    
private:
    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    
    std::vector<Frame> frames_;
    size_t capacity_;
    size_t head_ = 0;
    size_t tail_ = 0;
    size_t count_ = 0;
    
    size_t drop_count_ = 0;
};

} 

#endif