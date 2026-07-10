#include "SimVideo.h"
#include "SimResourceResolver.h"
#include <cerrno>
#include <cstring>
#include <thread>
#include <fstream>
#include <string>
#include <elog.h>
namespace hal {

#define SIMVID_LOG_TAG "SIMVID"

namespace {

const char* videoConfigKey(VideoPayloadType payload) {
    switch (payload) {
        case VideoPayloadType::H264:
            return "h264";
        case VideoPayloadType::H265:
            return "h265";
        case VideoPayloadType::JPEG:
            return "jpg";
        default:
            return nullptr;
    }
}

const char* videoPayloadName(VideoPayloadType payload) {
    switch (payload) {
        case VideoPayloadType::H264:
            return "H264";
        case VideoPayloadType::H265:
            return "H265";
        case VideoPayloadType::JPEG:
            return "JPEG";
        default:
            return "UNKNOWN";
    }
}

} // namespace

SimVideoStream::SimVideoStream()
    : configured_(false),
      started_(false),
      last_pts_(0),
      frame_index_(0),
      src_(nullptr),
      src_len_(0),
      read_offset_(0) {}

SimVideoStream::~SimVideoStream() {
    stop();
}

bool SimVideoStream::configure(const VideoStreamConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    cfg_ = cfg;
    configured_ = true;
    last_pts_ = 0;
    frame_index_ = 0;
    return true;
}

bool SimVideoStream::start() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!configured_) {
        return false;
    }

    started_ = false;
    file_path_.clear();
    file_buf_.clear();
    last_buffer_.clear();
    last_pieces_.clear();
    src_ = nullptr;
    src_len_ = 0;

    std::string config_path;
    std::string cfg;
    if (!sim_resource::loadConfig(config_path, cfg)) {
        elog_e(SIMVID_LOG_TAG, "Failed to locate simulation config.json (checked SIM_RESOURCE_CONFIG, SIM_RESOURCE_DIR, exe_dir/res)");
        return false;
    }

    const char* key = videoConfigKey(cfg_.payload);
    if (!key) {
        elog_e(SIMVID_LOG_TAG, "Unsupported video payload: %d", static_cast<int>(cfg_.payload));
        return false;
    }

    file_path_ = sim_resource::resolveAssetPath(config_path, cfg, key);
    if (file_path_.empty()) {
        elog_e(SIMVID_LOG_TAG, "Missing '%s' in config: %s", key, config_path.c_str());
        return false;
    }

    errno = 0;
    std::ifstream vf(file_path_, std::ios::binary);
    if (!vf.is_open()) {
        elog_e(SIMVID_LOG_TAG, "Failed to open %s source: config=%s file=%s err=%s",
               videoPayloadName(cfg_.payload), config_path.c_str(), file_path_.c_str(), std::strerror(errno));
        return false;
    }

    vf.seekg(0, std::ios::end);
    std::streampos sz = vf.tellg();
    vf.seekg(0, std::ios::beg);
    if (sz <= 0) {
        elog_e(SIMVID_LOG_TAG, "Empty %s source: file=%s", videoPayloadName(cfg_.payload), file_path_.c_str());
        return false;
    }

    file_buf_.resize(static_cast<size_t>(sz));
    vf.read(reinterpret_cast<char*>(file_buf_.data()), sz);
    if (!vf) {
        elog_e(SIMVID_LOG_TAG, "Failed to read %s source: file=%s", videoPayloadName(cfg_.payload), file_path_.c_str());
        file_buf_.clear();
        return false;
    }

    if (cfg_.payload == VideoPayloadType::JPEG) {
        last_buffer_.assign(file_buf_.begin(), file_buf_.end());
        src_ = nullptr;
        src_len_ = 0;
    } else {
        src_ = file_buf_.data();
        src_len_ = file_buf_.size();
    }

    frame_index_ = 0;
    last_pts_ = 0;
    read_offset_ = 0;
    next_frame_time_ = std::chrono::steady_clock::now();
    started_ = true;

    elog_i(SIMVID_LOG_TAG, "Opened %s source: config=%s file=%s size=%zu",
           videoPayloadName(cfg_.payload), config_path.c_str(), file_path_.c_str(), file_buf_.size());
    return true;
}

bool SimVideoStream::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    started_ = false;
    return true;
}

bool SimVideoStream::polling(int timeout_ms) {
    if (!started_) return false;
    auto now = std::chrono::steady_clock::now();
    if (now >= next_frame_time_) {
        return true;
    }
    auto wait = next_frame_time_ - now;
    auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wait).count();
    if (timeout_ms <= 0 || wait_ms > timeout_ms) {
        std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 1));
        return false;
    }
    std::this_thread::sleep_for(wait);
    return true;
}

static inline bool is_start_code4(const uint8_t* p) {
    return p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x01;
}
static inline bool is_start_code3(const uint8_t* p) {
    return p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01;
}
static inline size_t find_start_code(const uint8_t* src, size_t len, size_t pos) {
    size_t i = pos;
    while (i + 3 < len) {
        if (is_start_code4(src + i)) return i;
        if (is_start_code3(src + i)) return i;
        i++;
    }
    return len;
}
static inline size_t start_code_len(const uint8_t* src, size_t pos) {
    return is_start_code4(src + pos) ? 4 : 3;
}
static inline int nal_type_h264(const uint8_t* src, size_t pos) {
    size_t sc = start_code_len(src, pos);
    return src[pos + sc] & 0x1F;
}
static inline int nal_type_h265(const uint8_t* src, size_t pos) {
    size_t sc = start_code_len(src, pos);
    return (src[pos + sc] >> 1) & 0x3F;
}
static inline bool is_vcl_h264(int t) { return t >= 1 && t <= 5; }
static inline bool is_idr_h264(int t) { return t == 5; }
static inline bool is_vcl_h265(int t) { return t >= 0 && t <= 31; }
static inline bool is_idr_h265(int t) { return t == 19 || t == 20 || t == 21; }

void SimVideoStream::buildSampleBitstream(bool key) {
    last_buffer_.clear();
    if (cfg_.payload == VideoPayloadType::JPEG && !file_buf_.empty()) {
        last_buffer_.assign(file_buf_.begin(), file_buf_.end());
    }
    last_pieces_.clear();
    VideoEncodedPiece p;
    p.data = last_buffer_.data();
    p.size = last_buffer_.size();
    last_pieces_.push_back(p);
}

uint64_t SimVideoStream::fpsIntervalUs() const {
    int num = cfg_.fps_num > 0 ? cfg_.fps_num : 25;
    int den = cfg_.fps_den > 0 ? cfg_.fps_den : 1;
    return (uint64_t)1000000ULL * (uint64_t)den / (uint64_t)num;
}

void SimVideoStream::buildNextFrame(VideoEncodedFrame& out) {
    last_buffer_.clear();
    last_pieces_.clear();
    if (src_ == nullptr || src_len_ == 0) {
        bool key = (frame_index_ % (cfg_.gop > 0 ? cfg_.gop : 25)) == 0;
        buildSampleBitstream(key);
        out.pieces = last_pieces_.data();
        out.piece_count = (int)last_pieces_.size();
        out.key = key;
        return;
    }
    size_t start = find_start_code(src_, src_len_, read_offset_);
    if (start == src_len_) {
        start = find_start_code(src_, src_len_, 0);
        read_offset_ = 0;
        if (start == src_len_) {
            bool key = false;
            buildSampleBitstream(key);
            out.pieces = last_pieces_.data();
            out.piece_count = (int)last_pieces_.size();
            out.key = key;
            return;
        }
    }
    bool is_h265 = cfg_.payload == VideoPayloadType::H265;
    size_t pos = start;
    size_t au_start = start;
    bool first_vcl_found = false;
    bool key = false;
    for (;;) {
        size_t sc_len = start_code_len(src_, pos);
        int t = is_h265 ? nal_type_h265(src_, pos) : nal_type_h264(src_, pos);
        bool is_vcl = is_h265 ? is_vcl_h265(t) : is_vcl_h264(t);
        bool is_idr = is_h265 ? is_idr_h265(t) : is_idr_h264(t);
        if (is_vcl) {
            first_vcl_found = true;
            if (is_idr) key = true;
            break;
        }
        size_t next = find_start_code(src_, src_len_, pos + sc_len);
        if (next == src_len_) {
            pos = find_start_code(src_, src_len_, 0);
            if (pos == src_len_) break;
        } else {
            pos = next;
        }
        if (pos == start) break;
    }
    size_t search_pos = pos + start_code_len(src_, pos);
    size_t next_vcl_start = find_start_code(src_, src_len_, search_pos);
    for (;;) {
        if (next_vcl_start == src_len_) {
            next_vcl_start = find_start_code(src_, src_len_, 0);
            if (next_vcl_start == src_len_) {
                next_vcl_start = start;
                break;
            }
        }
        int t2 = is_h265 ? nal_type_h265(src_, next_vcl_start) : nal_type_h264(src_, next_vcl_start);
        bool is_vcl2 = is_h265 ? is_vcl_h265(t2) : is_vcl_h264(t2);
        if (is_vcl2) break;
        size_t sc2 = start_code_len(src_, next_vcl_start);
        size_t next2 = find_start_code(src_, src_len_, next_vcl_start + sc2);
        if (next2 == src_len_) {
            next_vcl_start = find_start_code(src_, src_len_, 0);
            if (next_vcl_start == src_len_) {
                next_vcl_start = start;
                break;
            }
        } else {
            next_vcl_start = next2;
        }
        if (next_vcl_start == pos) break;
    }
    size_t end = next_vcl_start;
    if (!first_vcl_found) {
        end = start;
    }
    if (end >= au_start) {
        size_t frame_len = end - au_start;
        last_buffer_.resize(frame_len);
        std::memcpy(last_buffer_.data(), src_ + au_start, frame_len);
    } else {
        size_t tail_len = src_len_ - au_start;
        size_t head_len = end;
        last_buffer_.resize(tail_len + head_len);
        std::memcpy(last_buffer_.data(), src_ + au_start, tail_len);
        std::memcpy(last_buffer_.data() + tail_len, src_, head_len);
    }
    last_pieces_.clear();
    VideoEncodedPiece p;
    p.data = last_buffer_.data();
    p.size = last_buffer_.size();
    last_pieces_.push_back(p);
    out.key = key;
    read_offset_ = end;
}

bool SimVideoStream::getFrame(VideoEncodedFrame& out) {
    if (!started_) return false;
    if (cfg_.payload == VideoPayloadType::H264 || cfg_.payload == VideoPayloadType::H265) {
        buildNextFrame(out);
        out.pieces = last_pieces_.data();
        out.piece_count = (int)last_pieces_.size();
        out.pts = last_pts_;
    } else {
        bool key = (frame_index_ % (cfg_.gop > 0 ? cfg_.gop : 25)) == 0;
        buildSampleBitstream(key);
        out.pieces = last_pieces_.data();
        out.piece_count = (int)last_pieces_.size();
        out.pts = last_pts_;
        out.key = key;
    }
    frame_index_++;
    last_pts_ += fpsIntervalUs();
    next_frame_time_ = std::chrono::steady_clock::now() + std::chrono::microseconds(fpsIntervalUs());
    return true;
}

void SimVideoStream::releaseFrame(VideoEncodedFrame& out) {
    (void)out;
}

bool SimVideoStream::getInfo(VideoStreamInfo& info) {
    info.index = cfg_.channel.stream_index;
    info.enabled = started_ ? 1 : 0;
    info.sensor_index = cfg_.channel.sensor_index;
    info.output_index = cfg_.channel.stream_index;
    info.width = cfg_.width > 0 ? cfg_.width : 1920;
    info.height = cfg_.height > 0 ? cfg_.height : 1080;
    info.fps_num = cfg_.fps_num > 0 ? cfg_.fps_num : 25;
    info.fps_den = cfg_.fps_den > 0 ? cfg_.fps_den : 1;
    info.payload = cfg_.payload;
    info.ae_converged = true;
    info.ae_mean   = 0;
    info.ae_target = 0;
    return true;
}
bool SimVideoStream::requestIDR() {
    elog_i(SIMVID_LOG_TAG, "requestIDR ignored for simulation file source");
    return true;
}

SimVideo::SimVideo() {}
SimVideo::~SimVideo() {}
bool SimVideo::init() { return true; }
bool SimVideo::exit() { return true; }
std::shared_ptr<IVideoStream> SimVideo::createVideoStream() { return std::make_shared<SimVideoStream>(); }

SimVideoControl::SimVideoControl() : daynight_(ISPDaynightMode::DAY) {}
SimVideoControl::~SimVideoControl() {}
bool SimVideoControl::getISPMode(ISPDaynightMode& state) { state = daynight_; return true; }
bool SimVideoControl::setISPMode(ISPDaynightMode state) { daynight_ = state; return true; }

} // namespace hal
