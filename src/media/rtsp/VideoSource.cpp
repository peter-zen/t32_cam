#include "VideoSource.h"
#include <cstring>
#include <cstdlib>

using namespace media;

VideoSource::VideoSource(std::shared_ptr<hal::IVideoStream> stream)
    : stream_(std::move(stream)), open_(false) {}

bool VideoSource::open() {
    if (open_) return true;
    if (!stream_) return false;
    if (!stream_->start()) return false;
    open_ = true;
    return true;
}

void VideoSource::close() {
    if (stream_) {
        stream_->stop();
    }
    open_ = false;
}

bool VideoSource::isOpen() const {
    return open_;
}

int VideoSource::pullData(void** data, size_t* size, uint64_t* timestamp) {
    if (!stream_) return -1;
    if (!stream_->polling(100)) return -1;
    hal::VideoEncodedFrame frame;
    if (!stream_->getFrame(frame)) return -1;
    size_t total = 0;
    for (size_t i = 0; i < frame.piece_count; ++i) {
        total += frame.pieces[i].size;
    }
    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf) {
        stream_->releaseFrame(frame);
        return -1;
    }
    size_t off = 0;
    for (size_t i = 0; i < frame.piece_count; ++i) {
        memcpy(buf + off, frame.pieces[i].data, frame.pieces[i].size);
        off += frame.pieces[i].size;
    }
    if (timestamp) {
        *timestamp = frame.pts;
    }
    *data = buf;
    *size = total;
    stream_->releaseFrame(frame);
    return 0;
}

int VideoSource::releaseData(void** data, size_t* size, uint64_t* timestamp) {
    (void)timestamp;
    if (data && *data) {
        free(*data);
        *data = nullptr;
    }
    if (size) *size = 0;
    return 0;
}

bool VideoSource::requestIDR() {
    if (stream_) {
        return stream_->requestIDR();
    }
    return false;
}

MediaType VideoSource::getMediaType() const {
    return MediaType::VIDEO;
}

MediaParams VideoSource::getParams() const {
    MediaParams p;
    p.type = MediaType::VIDEO;
    if (stream_) {
        hal::VideoStreamInfo info;
        if (stream_->getInfo(info)) {
            p.videoWidth = info.width;
            p.videoHeight = info.height;
            p.videoFrameRate = (info.fps_den > 0) ? (info.fps_num / info.fps_den) : 15;
            if (info.payload == hal::VideoPayloadType::H264) {
                p.videoCodec = VideoCodec::H264;
            } else if (info.payload == hal::VideoPayloadType::H265) {
                p.videoCodec = VideoCodec::H265;
            }
        }
    }
    return p;
}
