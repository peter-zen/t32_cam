/**
 * @file MediaFIFO.cpp
 * @brief 统一的媒体FIFO缓冲区实现
 */

#include "MediaFIFO.h"
#include <cstring>
#include <elog.h>

#define LOG_TAG "FIFO"

namespace media {

template<typename T>
MediaFIFO<T>::MediaFIFO(size_t capacity)
    : capacity_(capacity)
    , drop_count_(0)
{
    frames_.resize(capacity_);
}

template<typename T>
MediaFIFO<T>::~MediaFIFO()
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (size_t i = 0; i < frames_.size(); i++) {
        if (frames_[i].data) {
            free(frames_[i].data);
            frames_[i].data = nullptr;
        }
    }
}

template<typename T>
bool MediaFIFO<T>::push(void* data, size_t size, uint64_t timestamp, bool isKeyFrame)
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (count_ >= capacity_) {
        drop_count_++;
        return false;
    }
    
    Frame& frame = frames_[tail_];
    
    if (!frame.data || size > frame.size) {
        if (frame.data) {
            free(frame.data);
        }
        frame.data = malloc(size);
        if (!frame.data) {
            elog_e(LOG_TAG, "Failed to allocate buffer: %zu", size);
            return false;
        }
    }
    
    memcpy(frame.data, data, size);
    frame.size = size;
    frame.timestamp_us = timestamp;
    frame.isKeyFrame = isKeyFrame;
    
    tail_ = (tail_ + 1) % capacity_;
    count_++;
    
    return true;
}

template<typename T>
bool MediaFIFO<T>::pushBlocking(void* data, size_t size, uint64_t timestamp,
                              std::chrono::milliseconds timeout, bool isKeyFrame)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (count_ >= capacity_) {
        if (not_full_.wait_for(lock, timeout) == std::cv_status::timeout) {
            elog_w(LOG_TAG, "FIFO full, push timeout");
            return false;
        }
    }

    if (count_ >= capacity_) {
        return false;
    }

    Frame& frame = frames_[tail_];

    if (!frame.data || size > frame.size) {
        if (frame.data) {
            free(frame.data);
        }
        frame.data = malloc(size);
        if (!frame.data) {
            elog_e(LOG_TAG, "Failed to allocate buffer: %zu", size);
            return false;
        }
    }

    memcpy(frame.data, data, size);
    frame.size = size;
    frame.timestamp_us = timestamp;
    frame.isKeyFrame = isKeyFrame;

    tail_ = (tail_ + 1) % capacity_;
    count_++;

    return true;
}

template<typename T>
bool MediaFIFO<T>::pushOrDrop(void* data, size_t size, uint64_t timestamp, bool isKeyFrame)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (count_ >= capacity_) {
        elog_w(LOG_TAG, "FIFO full, dropping oldest frame for new frame");
        drop_count_++;

        Frame& oldFrame = frames_[head_];
        if (oldFrame.data) {
            free(oldFrame.data);
            oldFrame.data = nullptr;
        }

        head_ = (head_ + 1) % capacity_;
        count_--;

        lock.unlock();
        not_empty_.notify_one();
        lock.lock();
    }

    Frame& frame = frames_[tail_];

    if (!frame.data || size > frame.size) {
        if (frame.data) {
            free(frame.data);
        }
        frame.data = malloc(size);
        if (!frame.data) {
            elog_e(LOG_TAG, "Failed to allocate buffer: %zu", size);
            return false;
        }
    }

    memcpy(frame.data, data, size);
    frame.size = size;
    frame.timestamp_us = timestamp;
    frame.isKeyFrame = isKeyFrame;

    tail_ = (tail_ + 1) % capacity_;
    count_++;

    return true;
}

template<typename T>
bool MediaFIFO<T>::pop(Frame& frame)
{
    if (count_ == 0) {
        return false;
    }
    
    frame = frames_[head_];
    return true;
}

template<typename T>
bool MediaFIFO<T>::popBlocking(Frame& frame, std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (!not_empty_.wait_for(lock, timeout, [this] { return count_ > 0; })) {
        return false;
    }

    if (count_ == 0) {
        return false;
    }

    frame = frames_[head_];
    return true;
}

template<typename T>
void MediaFIFO<T>::release(const Frame& frame)
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (count_ == 0) {
        elog_w(LOG_TAG, "Attempted to release from empty FIFO");
        return;
    }
    
    head_ = (head_ + 1) % capacity_;
    count_--;
    
    lock.unlock();
    not_full_.notify_one();
}

template<typename T>
size_t MediaFIFO<T>::size() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    return count_;
}

template<typename T>
bool MediaFIFO<T>::empty() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    return count_ == 0;
}

template<typename T>
bool MediaFIFO<T>::full() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    return count_ >= capacity_;
}

template<typename T>
bool MediaFIFO<T>::frontIsKeyFrame() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (count_ == 0) {
        return false;
    }
    return frames_[head_].isKeyFrame;
}

template<typename T>
bool MediaFIFO<T>::dropOldest()
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (count_ == 0) {
        return false;
    }
    
    drop_count_++;
    
    // We don't free data here, we just advance head.
    // The data buffer will be reused or freed when overwritten by push()
    // or when the FIFO is destroyed.
    // However, if we want to be clean or if data ownership is tricky, 
    // we might want to free it. But current push() implementation frees
    // old data if needed before allocating new. 
    // Wait, push() checks `if (frame.data) free(frame.data)`.
    // So simply advancing head is safe IF we don't care about memory usage 
    // of "ghost" frames until they are overwritten.
    // To be safe and minimize peak memory, let's free it now.
    
    Frame& oldFrame = frames_[head_];
    if (oldFrame.data) {
        free(oldFrame.data);
        oldFrame.data = nullptr;
    }
    
    head_ = (head_ + 1) % capacity_;
    count_--;
    
    lock.unlock();
    not_full_.notify_one();
    return true;
}

template<typename T>
bool MediaFIFO<T>::dropFirstNonKeyFrame()
{
    std::unique_lock<std::mutex> lock(mutex_);
    
    if (count_ == 0) {
        return false;
    }
    
    // Find the first non-key frame
    // We need to iterate from head to tail
    size_t current = head_;
    size_t steps = 0;
    int targetIndex = -1;
    
    while (steps < count_) {
        if (!frames_[current].isKeyFrame) {
            targetIndex = current;
            break;
        }
        current = (current + 1) % capacity_;
        steps++;
    }
    
    if (targetIndex == -1) {
        // No non-key frame found
        return false;
    }
    
    drop_count_++;
    
    // Remove the frame at targetIndex
    // Since this is a circular buffer implemented with vector, removing from middle is tricky.
    // We have to shift elements to fill the gap.
    // Given FIFO size is small (e.g. 60), shifting is acceptable.
    // Shift elements from [head, targetIndex-1] one step to the right (towards targetIndex)
    // effectively moving head one step forward.
    
    Frame& targetFrame = frames_[targetIndex];
    if (targetFrame.data) {
        free(targetFrame.data);
        targetFrame.data = nullptr;
    }
    
    // Shift elements from head to targetIndex
    // We move elements "forward" (towards tail) to fill the gap? 
    // No, usually we pull elements back.
    // Actually, easier to move elements from [head, targetIndex-1] into [head+1, targetIndex]
    // and then advance head.
    
    // Example: [H, A, B, T, C, D] -> T is target.
    // Move B to T, A to B, H to A.
    // Result: [?, H, A, B, C, D] -> New Head is old H's position + 1.
    
    // Careful with circular wrapping.
    
    size_t curr = targetIndex;
    size_t prev;
    
    // Iterate backwards from targetIndex to head
    for (size_t i = 0; i < steps; i++) {
        // prev is (curr - 1) handling wrap around
        prev = (curr == 0) ? (capacity_ - 1) : (curr - 1);
        
        // Move prev to curr
        frames_[curr] = frames_[prev];
        
        curr = prev;
    }
    
    // The slot at 'head' is now "empty" (moved to head+1), so we advance head
    // Note: The original head's data was moved to head+1. 
    // The Frame struct at old head_ now contains a copy of what was there (pointer).
    // But since we moved the ownership to the next slot, we should zero out the old slot 
    // to prevent double free if we were careless (though we just advance head so it's fine).
    // Better to nullify it just in case.
    frames_[head_].data = nullptr;
    frames_[head_].size = 0;
    
    head_ = (head_ + 1) % capacity_;
    count_--;
    
    lock.unlock();
    not_full_.notify_one();
    return true;
}

template<typename T>
size_t MediaFIFO<T>::getDropCount() const
{
    return drop_count_;
}

template<typename T>
void MediaFIFO<T>::resetStats()
{
    drop_count_ = 0;
}

template class MediaFIFO<uint8_t>;

}