#include "SimAudio.h"
#include "SimResourceResolver.h"
#include <cerrno>
#include <cmath>
#include <cstring>
#include <thread>
#include <fstream>
#include <string>
#include <elog.h>
namespace hal {

#define SIMAUD_LOG_TAG "SIMAUD"

namespace {

const char* audioPayloadName(AudioPayloadType payload) {
    switch (payload) {
        case AudioPayloadType::AAC:
            return "AAC";
        case AudioPayloadType::G711A:
            return "G711A";
        case AudioPayloadType::G711U:
            return "G711U";
        case AudioPayloadType::PCM16:
            return "PCM16";
        default:
            return "UNKNOWN";
    }
}

} // namespace

SimAudioStream::SimAudioStream()
    : configured_(false),
      started_(false),
      last_pts_(0),
      i_(0),
      src_(nullptr),
      src_len_(0),
      read_offset_(0) {}

SimAudioStream::~SimAudioStream() {
    stop();
}

bool SimAudioStream::configure(const AudioStreamConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    cfg_ = cfg;
    configured_ = true;
    last_pts_ = 0;
    i_ = 0;
    return true;
}

bool SimAudioStream::start() {
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
    last_pts_ = 0;
    i_ = 0;

    std::string config_path;
    std::string cfg;
    if (!sim_resource::loadConfig(config_path, cfg)) {
        elog_w(SIMAUD_LOG_TAG, "Failed to locate simulation config.json, using synthetic %s audio",
               audioPayloadName(cfg_.payload));
        read_offset_ = 0;
        started_ = true;
        return true;
    }

    if (cfg_.payload == AudioPayloadType::AAC) {
        file_path_ = sim_resource::resolveAssetPath(config_path, cfg, "aac");
    } else if (cfg_.payload == AudioPayloadType::G711A) {
        file_path_ = sim_resource::resolveAssetPath(config_path, cfg, "g711a");
    } else if (cfg_.payload == AudioPayloadType::G711U) {
        file_path_ = sim_resource::resolveAssetPath(config_path, cfg, "g711u");
    } else if (cfg_.payload == AudioPayloadType::PCM16) {
        file_path_ = sim_resource::resolveAssetPath(config_path, cfg, "pcm");
        if (file_path_.empty()) {
            file_path_ = sim_resource::resolveAssetPath(config_path, cfg, "g711u");
        }
    }

    if (!file_path_.empty()) {
        errno = 0;
        std::ifstream af(file_path_, std::ios::binary);
        if (af.is_open()) {
            af.seekg(0, std::ios::end);
            std::streampos sz = af.tellg();
            af.seekg(0, std::ios::beg);
            if (sz > 0) {
                file_buf_.resize(static_cast<size_t>(sz));
                af.read(reinterpret_cast<char*>(file_buf_.data()), sz);
                if (af) {
                    src_ = file_buf_.data();
                    src_len_ = file_buf_.size();
                    elog_i(SIMAUD_LOG_TAG, "Opened %s source: config=%s file=%s size=%zu",
                           audioPayloadName(cfg_.payload), config_path.c_str(), file_path_.c_str(), file_buf_.size());
                } else {
                    file_buf_.clear();
                    elog_w(SIMAUD_LOG_TAG, "Failed to read %s source: file=%s, using synthetic audio",
                           audioPayloadName(cfg_.payload), file_path_.c_str());
                }
            } else {
                elog_w(SIMAUD_LOG_TAG, "Empty %s source: file=%s, using synthetic audio",
                       audioPayloadName(cfg_.payload), file_path_.c_str());
            }
        } else {
            elog_w(SIMAUD_LOG_TAG, "Failed to open %s source: config=%s file=%s err=%s, using synthetic audio",
                   audioPayloadName(cfg_.payload), config_path.c_str(), file_path_.c_str(), std::strerror(errno));
        }
    } else {
        elog_w(SIMAUD_LOG_TAG, "Missing configured %s source in %s, using synthetic audio",
               audioPayloadName(cfg_.payload), config_path.c_str());
    }

    read_offset_ = 0;
    started_ = true;
    return true;
}

bool SimAudioStream::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    started_ = false;
    return true;
}

bool SimAudioStream::polling(int timeout_ms) {
    if (!started_) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 10));
    return true;
}

uint8_t SimAudioStream::linear_to_alaw(int16_t pcm_val) {
    const int16_t ALAW_MAX = 0x0FFF;
    int16_t sign = (pcm_val & 0x8000) >> 8;
    if (sign != 0) pcm_val = -pcm_val - 1;
    if (pcm_val > ALAW_MAX) pcm_val = ALAW_MAX;
    int16_t exponent = 7;
    for (int mask = 0x400; (pcm_val & mask) == 0 && exponent > 0; mask >>= 1) {
        exponent--;
    }
    int16_t mantissa = (pcm_val >> ((exponent == 0) ? 4 : (exponent + 3))) & 0x0F;
    uint8_t alaw = (uint8_t)(sign | (exponent << 4) | mantissa);
    return alaw ^ 0xD5;
}

void SimAudioStream::buildPcmFrame() {
    int samples = cfg_.num_per_frame > 0 ? cfg_.num_per_frame : 320;
    int channels = cfg_.channels > 0 ? cfg_.channels : 1;
    last_buffer_.resize(samples * channels * 2);
    double freq = 440.0;
    double sr = (double)(cfg_.sample_rate > 0 ? cfg_.sample_rate : 16000);
    for (int n = 0; n < samples; ++n) {
        double t = (double)(i_ * samples + n) / sr;
        int16_t val = (int16_t)(std::sin(2.0 * M_PI * freq * t) * 3000);
        for (int ch = 0; ch < channels; ++ch) {
            size_t off = (n * channels + ch) * 2;
            last_buffer_[off + 0] = (uint8_t)((val >> 8) & 0xFF);
            last_buffer_[off + 1] = (uint8_t)(val & 0xFF);
        }
    }
}

void SimAudioStream::buildG711AFrame() {
    int samples = cfg_.num_per_frame > 0 ? cfg_.num_per_frame : 320;
    int channels = cfg_.channels > 0 ? cfg_.channels : 1;
    last_buffer_.resize(samples * channels);
    double freq = 440.0;
    double sr = (double)(cfg_.sample_rate > 0 ? cfg_.sample_rate : 16000);
    for (int n = 0; n < samples; ++n) {
        double t = (double)(i_ * samples + n) / sr;
        int16_t val = (int16_t)(std::sin(2.0 * M_PI * freq * t) * 3000);
        for (int ch = 0; ch < channels; ++ch) {
            size_t off = (n * channels + ch);
            last_buffer_[off] = linear_to_alaw(val);
        }
    }
}

static inline bool is_adts_sync(const uint8_t* p) {
    return p[0] == 0xFF && (p[1] & 0xF0) == 0xF0;
}
bool SimAudioStream::adts_find_frame(const uint8_t* src, size_t len, size_t start, size_t& frame_off, size_t& frame_len, int& samples, int& sample_rate) {
    if (len < 7) return false;
    size_t i = start;
    while (i + 7 <= len && !is_adts_sync(src + i)) {
        i++;
    }
    if (i + 7 > len) {
        i = 0;
        while (i + 7 <= len && !is_adts_sync(src + i)) i++;
        if (i + 7 > len) return false;
    }
    frame_off = i;
    int aac_frame_length = ((src[i + 3] & 0x03) << 11) | (src[i + 4] << 3) | ((src[i + 5] & 0xE0) >> 5);
    frame_len = (size_t)aac_frame_length;
    samples = 1024;
    int sf_index = (src[i + 2] & 0x3C) >> 2;
    static const int sf_table[16] = {96000,88200,64000,48000,44100,32000,24000,22050,16000,12000,11025,8000,7350,0,0,0};
    sample_rate = (sf_index >= 0 && sf_index < 16) ? sf_table[sf_index] : 16000;
    if (frame_off + frame_len > len) {
        return false;
    }
    return true;
}

void SimAudioStream::buildNextFrame(AudioEncodedFrame& out) {
    last_buffer_.clear();
    last_pieces_.clear();
    if (!src_ || src_len_ == 0) {
        if (cfg_.payload == AudioPayloadType::PCM16) {
            buildPcmFrame();
        } else {
            buildG711AFrame();
        }
        AudioEncodedPiece p;
        p.data = last_buffer_.data();
        p.size = last_buffer_.size();
        last_pieces_.push_back(p);
        out.key = true;
        return;
    }
    if (cfg_.payload == AudioPayloadType::AAC) {
        size_t off = 0, flen = 0;
        int samples = 1024, sr_actual = cfg_.sample_rate > 0 ? cfg_.sample_rate : 16000;
        bool ok = adts_find_frame(src_, src_len_, read_offset_, off, flen, samples, sr_actual);
        if (!ok) {
            read_offset_ = 0;
            ok = adts_find_frame(src_, src_len_, read_offset_, off, flen, samples, sr_actual);
            if (!ok) {
                return;
            }
        }
        if (off + flen <= src_len_) {
            last_buffer_.assign(src_ + off, src_ + off + flen);
            read_offset_ = off + flen;
        } else {
            read_offset_ = 0;
            return;
        }
        AudioEncodedPiece p;
        p.data = last_buffer_.data();
        p.size = last_buffer_.size();
        last_pieces_.push_back(p);
        out.key = true;
        uint64_t inc = (uint64_t)1000000ULL * (uint64_t)samples / (uint64_t)sr_actual;
        last_pts_ += inc;
        return;
    } else {
        int samples = cfg_.num_per_frame > 0 ? cfg_.num_per_frame : 320;
        int channels = cfg_.channels > 0 ? cfg_.channels : 1;
        size_t bytes = (size_t)samples * (size_t)channels;
        last_buffer_.resize(bytes);
        size_t remain = src_len_ - read_offset_;
        if (remain >= bytes) {
            std::memcpy(last_buffer_.data(), src_ + read_offset_, bytes);
            read_offset_ += bytes;
        } else {
            if (remain > 0) {
                std::memcpy(last_buffer_.data(), src_ + read_offset_, remain);
            }
            size_t left = bytes - remain;
            if (left > 0) std::memcpy(last_buffer_.data() + remain, src_, left);
            read_offset_ = left;
        }
        AudioEncodedPiece p;
        p.data = last_buffer_.data();
        p.size = last_buffer_.size();
        last_pieces_.push_back(p);
        out.key = true;
        uint64_t inc = frameIntervalUs();
        last_pts_ += inc;
        return;
    }
}

bool SimAudioStream::getFrame(AudioEncodedFrame& out) {
    if (!started_) return false;
    out.pieces = nullptr;
    out.piece_count = 0;
    buildNextFrame(out);
    out.pieces = last_pieces_.data();
    out.piece_count = (int)last_pieces_.size();
    out.key = true;
    out.pts = last_pts_;
    i_++;
    return true;
}

void SimAudioStream::releaseFrame(AudioEncodedFrame& out) {
    (void)out;
}

bool SimAudioStream::getInfo(AudioStreamInfo& info) {
    info.index = cfg_.channel.channel_index;
    info.enabled = started_ ? 1 : 0;
    info.device_index = cfg_.channel.device_index;
    info.channel_index = cfg_.channel.channel_index;
    info.sample_rate = cfg_.sample_rate > 0 ? cfg_.sample_rate : 16000;
    info.channels = cfg_.channels > 0 ? cfg_.channels : 1;
    info.bit_width = cfg_.bit_width > 0 ? cfg_.bit_width : 16;
    info.payload = cfg_.payload;
    return true;
}

bool SimAudioStream::getCodecConfig(void*& data, size_t& size) {
    data = nullptr;
    size = 0;
    return false;
}

uint64_t SimAudioStream::frameIntervalUs() const {
    int sr = cfg_.sample_rate > 0 ? cfg_.sample_rate : 16000;
    int n = cfg_.num_per_frame > 0 ? cfg_.num_per_frame : 320;
    return (uint64_t)1000000ULL * (uint64_t)n / (uint64_t)sr;
}

SimAudio::SimAudio() {}
SimAudio::~SimAudio() {}
bool SimAudio::init() { return true; }
bool SimAudio::exit() { return true; }
std::shared_ptr<IAudioStream> SimAudio::createAudioStream() {
    return std::make_shared<SimAudioStream>();
}

} // namespace hal
