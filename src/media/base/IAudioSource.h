#ifndef IAUDIO_SOURCE_H
#define IAUDIO_SOURCE_H

#include "IMediaSource.h"
#include "MediaTypes.h"

namespace media {

class IAudioSource : public IMediaSource {
 public:
    virtual ~IAudioSource() = default;

    virtual AudioCodec getAudioCodec() const = 0;

    MediaType getMediaType() const override {
        return MediaType::AUDIO;
    }
};

} // namespace media

#endif // IAUDIO_SOURCE_H
