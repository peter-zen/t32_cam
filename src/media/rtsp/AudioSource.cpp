#include "AudioSource.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

using namespace media;

AudioSource::AudioSource(std::shared_ptr<hal::IAudioStream> stream,
                         int pollTimeoutMs,
                         int frameDurationUs)
    : stream_(std::move(stream))
    , open_(false)
    , pollTimeoutMs_(std::max(1, pollTimeoutMs))
    , frameDurationUs_(std::max(0, frameDurationUs))
{
}

bool AudioSource::open() {
    if (open_) return true;
    if (!stream_) return false;
    if (!stream_->start()) return false;
    open_ = true;
    return true;
}

void AudioSource::close() {
    if (stream_) {
        stream_->stop();
    }
    open_ = false;
}

bool AudioSource::isOpen() const {
    return open_;
}

int AudioSource::pullData(void** data, size_t* size, uint64_t* timestamp) {
    if (!stream_) return -1;
    if (!stream_->polling(pollTimeoutMs_)) return -1;
    hal::AudioEncodedFrame frame;
    if (!stream_->getFrame(frame)) return -1;
    size_t total = 0;
    for (int i = 0; i < frame.piece_count; ++i) {
        total += frame.pieces[i].size;
    }
    uint8_t* buf = (uint8_t*)malloc(total);
    if (!buf) {
        stream_->releaseFrame(frame);
        return -1;
    }
    size_t off = 0;
    for (int i = 0; i < frame.piece_count; ++i) {
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

int AudioSource::releaseData(void** data, size_t* size, uint64_t* timestamp) {
    (void)timestamp;
    if (data && *data) {
        free(*data);
        *data = nullptr;
    }
    if (size) *size = 0;
    return 0;
}

MediaType AudioSource::getMediaType() const {
    return MediaType::AUDIO;
}

MediaParams AudioSource::getParams() const {
    MediaParams p;
    p.type = MediaType::AUDIO;
    p.audioFrameDurationUs = frameDurationUs_;
    if (stream_) {
        hal::AudioStreamInfo info;
        if (stream_->getInfo(info)) {
            p.sampleRate = info.sample_rate;
            p.channels = info.channels;
            p.bitsPerSample = info.bit_width;
            if (info.payload == hal::AudioPayloadType::G711A) {
                p.audioCodec = AudioCodec::PCMA;
            } else if (info.payload == hal::AudioPayloadType::G711U) {
                p.audioCodec = AudioCodec::PCMU;
            } else if (info.payload == hal::AudioPayloadType::AAC) {
                p.audioCodec = AudioCodec::AAC;
            } else if (info.payload == hal::AudioPayloadType::PCM16) {
                p.audioCodec = AudioCodec::L16;
            }
        }
    } else {
        p.sampleRate = 16000;
        p.channels = 1;
        p.bitsPerSample = 16;
    }
    return p;
}
