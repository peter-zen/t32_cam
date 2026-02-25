/**
 * @file IMediaSource.h
 * @brief 媒体源基类 - 定义统一的媒体源接口
 */

#ifndef IMEDIA_SOURCE_H
#define IMEDIA_SOURCE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include "MediaTypes.h"

namespace media {

class IMediaSource {
public:
    virtual ~IMediaSource() = default;
    
    virtual bool open() = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    
    virtual int pullData(void** data, size_t* size, uint64_t* timestamp) = 0;
    virtual int releaseData(void** data, size_t* size, uint64_t* timestamp) = 0;
    
    virtual void reset() {}
    virtual MediaType getMediaType() const = 0;
    
    virtual MediaParams getParams() const = 0;
};

} 

#endif