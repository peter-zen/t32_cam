#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <memory>

namespace hal {

enum class AudioPayloadType {
    G711A,
    G711U,
    AAC,
    PCM16
};

enum class AudioSoundMode {
    MONO,
    STEREO
};

enum class AudioInputType {
    AUTO,
    AI,
    DMIC
};

struct AudioStreamInfo {
    int index;
    int enabled;
    int device_index;
    int channel_index;
    int sample_rate;
    int channels;
    int bit_width;
    AudioPayloadType payload;
};

struct AudioEncodedPiece {
    void* data;
    size_t size;
};

struct AudioEncodedFrame {
    AudioEncodedPiece* pieces;
    int piece_count;
    uint64_t pts;
    bool key;
};

struct AudioChannelId {
    int device_index;
    int channel_index;
};

struct AudioStreamConfig {
    AudioPayloadType payload;
    AudioChannelId channel;
    int sample_rate;
    int channels;
    int bit_width;
    int num_per_frame;
    int frame_num;
    int volume;
    int gain;
    unsigned long bitrate_per_channel;
    int quality;
    AudioInputType input;
};

class IAudioStream {
public:
    virtual ~IAudioStream() {}
    virtual bool configure(const AudioStreamConfig& cfg) = 0;
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool polling(int timeout_ms) = 0;
    virtual bool getFrame(AudioEncodedFrame& out) = 0;
    virtual void releaseFrame(AudioEncodedFrame& out) = 0;
    virtual bool getInfo(AudioStreamInfo& info) = 0;
    virtual bool getCodecConfig(void*& data, size_t& size) = 0;
};

class IAudio {
public:
    virtual ~IAudio() {}
    virtual bool init() = 0;
    virtual bool exit() = 0;
    virtual std::shared_ptr<IAudioStream> createAudioStream() = 0;
};

} // namespace hal
