#ifndef IVIDEO_SOURCE_H
#define IVIDEO_SOURCE_H

#include "IMediaSource.h"
#include "MediaTypes.h"

namespace media {

class IVideoSource : public IMediaSource {
 public:
    virtual ~IVideoSource() = default;

    virtual VideoCodec getVideoCodec() const = 0;

    MediaType getMediaType() const override {
        return MediaType::VIDEO;
    }
};

} // namespace media

#endif // IVIDEO_SOURCE_H
