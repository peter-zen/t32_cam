#pragma once
#include "IVideo.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
namespace hal {

class SimVideoStream : public IVideoStream {
public:
    SimVideoStream();
    ~SimVideoStream() override;
    bool configure(const VideoStreamConfig& cfg) override;
    bool start() override;
    bool stop() override;
    bool polling(int timeout_ms) override;
    bool getFrame(VideoEncodedFrame& out) override;
    void releaseFrame(VideoEncodedFrame& out) override;
    bool getInfo(VideoStreamInfo& info) override;
    bool requestIDR() override;

private:
    void buildSampleBitstream(bool key);
    uint64_t fpsIntervalUs() const;
    void buildNextFrame(VideoEncodedFrame& out);

private:
    VideoStreamConfig cfg_;
    bool configured_;
    bool started_;
    std::mutex mtx_;
    std::vector<uint8_t> last_buffer_;
    std::vector<VideoEncodedPiece> last_pieces_;
    uint64_t last_pts_;
    uint64_t frame_index_;
    std::chrono::steady_clock::time_point next_frame_time_;
    const uint8_t* src_;
    size_t src_len_;
    size_t read_offset_;
    std::vector<uint8_t> file_buf_;
    std::string file_path_;
};

class SimVideo : public IVideo {
public:
    SimVideo();
    ~SimVideo() override;
    bool init() override;
    bool exit() override;
    std::shared_ptr<IVideoStream> createVideoStream() override;
};

class SimVideoControl : public IVideoControl {
public:
    SimVideoControl();
    ~SimVideoControl() override;
    bool getISPMode(ISPDaynightMode& state) override;
    bool setISPMode(ISPDaynightMode state) override;
private:
    ISPDaynightMode daynight_;
};

} // namespace hal
