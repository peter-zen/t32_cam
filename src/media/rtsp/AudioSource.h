#ifndef AUDIO_SOURCE_H
#define AUDIO_SOURCE_H

#include <memory>
#include "../base/IMediaSource.h"
#include "IAudio.h"

namespace media {

class AudioSource : public IMediaSource {
public:
    explicit AudioSource(std::shared_ptr<hal::IAudioStream> stream);
    bool open() override;
    void close() override;
    bool isOpen() const override;
    int pullData(void** data, size_t* size, uint64_t* timestamp) override;
    int releaseData(void** data, size_t* size, uint64_t* timestamp) override;
    MediaType getMediaType() const override;
    MediaParams getParams() const override;
private:
    std::shared_ptr<hal::IAudioStream> stream_;
    bool open_;
};

} // namespace media

#endif
