#ifndef VIDEO_SOURCE_H
#define VIDEO_SOURCE_H

#include <memory>
#include "../base/IMediaSource.h"
#include "IVideo.h"

namespace media {

class VideoSource : public IMediaSource {
public:
    explicit VideoSource(std::shared_ptr<hal::IVideoStream> stream);
    bool open() override;
    void close() override;
    bool isOpen() const override;
    int pullData(void** data, size_t* size, uint64_t* timestamp) override;
    int releaseData(void** data, size_t* size, uint64_t* timestamp) override;
    bool requestIDR() override;
    MediaType getMediaType() const override;
    MediaParams getParams() const override;
private:
    std::shared_ptr<hal::IVideoStream> stream_;
    bool open_;
};

} // namespace media

#endif
