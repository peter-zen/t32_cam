#pragma once
#include "IAudio.h"
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
namespace hal {

class SimAudioStream : public IAudioStream {
public:
    SimAudioStream();
    ~SimAudioStream() override;
    bool configure(const AudioStreamConfig& cfg) override;
    bool start() override;
    bool stop() override;
    bool polling(int timeout_ms) override;
    bool getFrame(AudioEncodedFrame& out) override;
    void releaseFrame(AudioEncodedFrame& out) override;
    bool getInfo(AudioStreamInfo& info) override;
    bool getCodecConfig(void*& data, size_t& size) override;

private:
    void buildPcmFrame();
    void buildG711AFrame();
    static uint8_t linear_to_alaw(int16_t pcm_val);
    uint64_t frameIntervalUs() const;

private:
    AudioStreamConfig cfg_;
    bool configured_;
    bool started_;
    std::mutex mtx_;
    std::vector<uint8_t> last_buffer_;
    std::vector<AudioEncodedPiece> last_pieces_;
    uint64_t last_pts_;
    uint64_t i_;
    const uint8_t* src_;
    size_t src_len_;
    size_t read_offset_;
    std::vector<uint8_t> file_buf_;
    std::string file_path_;
    void buildNextFrame(AudioEncodedFrame& out);
    static bool adts_find_frame(const uint8_t* src, size_t len, size_t start, size_t& frame_off, size_t& frame_len, int& samples, int& sample_rate);
};

class SimAudio : public IAudio {
public:
    SimAudio();
    ~SimAudio() override;
    bool init() override;
    bool exit() override;
    std::shared_ptr<IAudioStream> createAudioStream() override;
};

} // namespace hal
