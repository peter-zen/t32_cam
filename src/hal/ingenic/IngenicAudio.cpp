#include "IngenicAudio.h"
#include <faac.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

namespace hal {

static inline int soundmode_to_channels(AudioSoundMode m) { return m == AudioSoundMode::STEREO ? 2 : 1; }

IngenicAudioStream::IngenicAudioStream()
    : configured_(false),
      started_(false),
      ref_count_(0),
      dev_id_(0),
      chn_id_(0),
      use_dmic_(false),
      actual_channels_(0),
      actual_sample_rate_(0),
      actual_bit_width_(16),
      last_pts_(0),
      last_frame_valid_(false),
      dmic_last_frame_valid_(false),
      aac_handle_(nullptr),
      aac_input_samples_(0),
      aac_max_output_bytes_(0) {
}

IngenicAudioStream::~IngenicAudioStream() {
    if (last_frame_valid_) {
        if (!use_dmic_) {
            IMP_AI_ReleaseFrame(dev_id_, chn_id_, &last_frame_);
        }
        last_frame_valid_ = false;
    }
    if (dmic_last_frame_valid_) {
        IMP_DMIC_ReleaseFrame(dev_id_, chn_id_, &dmic_last_frame_);
        dmic_last_frame_valid_ = false;
    }
    if (started_) {
        if (use_dmic_) {
            IMP_DMIC_DisableChn(dev_id_, chn_id_);
        } else {
            IMP_AI_DisableChn(dev_id_, chn_id_);
        }
        started_ = false;
    }
    if (configured_) {
        if (use_dmic_) {
            IMP_DMIC_Disable(dev_id_);
        } else {
            IMP_AI_Disable(dev_id_);
        }
        configured_ = false;
    }
    closeAacEncoder();
}

bool IngenicAudioStream::configure(const AudioStreamConfig& cfg) {
    std::lock_guard<std::mutex> lock(mtx_);
    cfg_ = cfg;
    dev_id_ = cfg.channel.device_index;
    chn_id_ = cfg.channel.channel_index;
    use_dmic_ = false;
    auto try_dmic = [&]() -> bool {
        IMPDmicAttr da;
        memset(&da, 0, sizeof(da));
        da.samplerate = (IMPDmicSampleRate)cfg.sample_rate;
        da.bitwidth = (IMPDmicBitWidth)AUDIO_BIT_WIDTH_16;
        da.soundmode = (IMPDmicSoundMode)((cfg.channels == 2) ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_MONO);
        da.frmNum = cfg.frame_num;
        da.numPerFrm = cfg.num_per_frame;
        da.chnCnt = cfg.channels;
        if (IMP_DMIC_SetPubAttr(dev_id_, &da) != 0) return false;
        if (IMP_DMIC_Enable(dev_id_) != 0) return false;
        IMPDmicChnParam cp;
        memset(&cp, 0, sizeof(cp));
        cp.usrFrmDepth = cfg.frame_num;
        cp.Rev = 0;
        if (IMP_DMIC_SetChnParam(dev_id_, chn_id_, &cp) != 0) { IMP_DMIC_Disable(dev_id_); return false; }
        if (IMP_DMIC_EnableChn(dev_id_, chn_id_) != 0) { IMP_DMIC_Disable(dev_id_); return false; }
        IMP_DMIC_SetVol(dev_id_, chn_id_, cfg.volume);
        IMP_DMIC_SetGain(dev_id_, chn_id_, cfg.gain);
        use_dmic_ = true;
        actual_sample_rate_ = cfg.sample_rate;
        actual_channels_ = cfg.channels;
        actual_bit_width_ = cfg.bit_width;
        return true;
    };
    auto try_ai = [&]() -> bool {
        IMPAudioIOAttr io;
        memset(&io, 0, sizeof(io));
        io.samplerate = (IMPAudioSampleRate)cfg.sample_rate;
        io.bitwidth = (IMPAudioBitWidth)((cfg.bit_width == 16) ? AUDIO_BIT_WIDTH_16 : AUDIO_BIT_WIDTH_16);
        io.soundmode = (IMPAudioSoundMode)((cfg.channels == 2) ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_MONO);
        io.frmNum = cfg.frame_num;
        io.numPerFrm = cfg.num_per_frame;
        io.chnCnt = cfg.channels;
        if (IMP_AI_SetPubAttr(dev_id_, &io) != 0) return false;
        if (IMP_AI_Enable(dev_id_) != 0) return false;
        IMPAudioIChnParam chnParam;
        memset(&chnParam, 0, sizeof(chnParam));
        chnParam.usrFrmDepth = cfg.frame_num;
        chnParam.aecChn = AUDIO_AEC_CHANNEL_FIRST_LEFT;
        if (IMP_AI_SetChnParam(dev_id_, chn_id_, &chnParam) != 0) { IMP_AI_Disable(dev_id_); return false; }
        if (IMP_AI_EnableChn(dev_id_, chn_id_) != 0) { IMP_AI_Disable(dev_id_); return false; }
        IMPAudioIOAttr io_read;
        memset(&io_read, 0, sizeof(io_read));
        if (IMP_AI_GetPubAttr(dev_id_, &io_read) == 0) {
            actual_sample_rate_ = (int)io_read.samplerate;
            actual_channels_ = io_read.chnCnt;
            actual_bit_width_ = (io_read.bitwidth == AUDIO_BIT_WIDTH_16) ? 16 : 16;
        } else {
            actual_sample_rate_ = cfg.sample_rate;
            actual_channels_ = cfg.channels;
            actual_bit_width_ = cfg.bit_width;
        }
        IMP_AI_SetVol(dev_id_, chn_id_, cfg.volume);
        IMP_AI_SetGain(dev_id_, chn_id_, cfg.gain);
        use_dmic_ = false;
        return true;
    };
    bool configured_ok = false;
    if (cfg.input == AudioInputType::DMIC) {
        configured_ok = try_dmic();
    } else if (cfg.input == AudioInputType::AI) {
        configured_ok = try_ai();
    } else {
        configured_ok = try_dmic();
        if (!configured_ok) configured_ok = try_ai();
    }
    if (!configured_ok) return false;
    if (cfg.payload == AudioPayloadType::AAC) {
        if (!openAacEncoder()) return false;
    }
    configured_ = true;
    started_ = false;
    ref_count_ = 0;
    last_pts_ = 0;
    return true;
}

bool IngenicAudioStream::start() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!configured_) return false;
    if (!started_) {
        started_ = true;
    }
    ref_count_++;
    return true;
}

bool IngenicAudioStream::stop() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (!configured_) return false;
    if (ref_count_ <= 0) return true;
    ref_count_--;
    if (ref_count_ == 0 && started_) {
        if (use_dmic_) {
            IMP_DMIC_DisableChn(dev_id_, chn_id_);
        } else {
            IMP_AI_DisableChn(dev_id_, chn_id_);
        }
        started_ = false;
    }
    return true;
}

bool IngenicAudioStream::polling(int timeout_ms) {
    if (!started_) return false;
    if (use_dmic_) {
        return IMP_DMIC_PollingFrame(dev_id_, chn_id_, timeout_ms) == 0;
    } else {
        return IMP_AI_PollingFrame(dev_id_, chn_id_, timeout_ms) == 0;
    }
}

bool IngenicAudioStream::getFrame(AudioEncodedFrame& out) {
    if (!started_) return false;
    last_buffer_.clear();
    bool ok = false;
    if (use_dmic_) {
        if (IMP_DMIC_GetFrame(dev_id_, chn_id_, &dmic_last_frame_, BLOCK) != 0) return false;
        dmic_last_frame_valid_ = true;
        IMPAudioFrame tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.bitwidth = AUDIO_BIT_WIDTH_16;
        tmp.soundmode = (actual_channels_ == 2) ? AUDIO_SOUND_MODE_STEREO : AUDIO_SOUND_MODE_MONO;
        tmp.virAddr = reinterpret_cast<uint32_t*>(dmic_last_frame_.rawFrame.virAddr);
        tmp.len = dmic_last_frame_.rawFrame.len;
        if (cfg_.payload == AudioPayloadType::G711A) {
            ok = encodeG711A(tmp, last_buffer_);
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)cfg_.num_per_frame / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
        } else if (cfg_.payload == AudioPayloadType::G711U) {
            ok = encodeG711U(tmp, last_buffer_);
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)cfg_.num_per_frame / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
        } else if (cfg_.payload == AudioPayloadType::AAC) {
            ok = encodeAac(tmp, last_buffer_);
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)1024ULL / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
        } else if (cfg_.payload == AudioPayloadType::PCM16) {
            size_t bytes = tmp.len;
            const uint8_t* d = reinterpret_cast<const uint8_t*>(tmp.virAddr);
            last_buffer_.assign(d, d + bytes);
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)cfg_.num_per_frame / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
            ok = true;
        }
        IMP_DMIC_ReleaseFrame(dev_id_, chn_id_, &dmic_last_frame_);
        dmic_last_frame_valid_ = false;
        if (!ok) return false;
    } else {
        if (IMP_AI_GetFrame(dev_id_, chn_id_, &last_frame_, BLOCK) != 0) return false;
        last_frame_valid_ = true;
        if (cfg_.payload == AudioPayloadType::G711A) {
            ok = encodeG711A(last_frame_, last_buffer_);
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)cfg_.num_per_frame / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
        } else if (cfg_.payload == AudioPayloadType::G711U) {
            ok = encodeG711U(last_frame_, last_buffer_);
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)cfg_.num_per_frame / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
        } else if (cfg_.payload == AudioPayloadType::AAC) {
            ok = encodeAac(last_frame_, last_buffer_);
            if (!ok) { IMP_AI_ReleaseFrame(dev_id_, chn_id_, &last_frame_); last_frame_valid_ = false; return false; }
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)1024ULL / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
        } else if (cfg_.payload == AudioPayloadType::PCM16) {
            size_t bytes = last_frame_.len;
            const uint8_t* d = reinterpret_cast<const uint8_t*>(last_frame_.virAddr);
            last_buffer_.assign(d, d + bytes);
            uint64_t inc = (uint64_t)1000000ULL * (uint64_t)cfg_.num_per_frame / (uint64_t)actual_sample_rate_;
            last_pts_ += inc;
            ok = true;
        }
        IMP_AI_ReleaseFrame(dev_id_, chn_id_, &last_frame_);
        last_frame_valid_ = false;
        if (!ok) return false;
    }
    last_pieces_.resize(1);
    last_pieces_[0].data = last_buffer_.data();
    last_pieces_[0].size = last_buffer_.size();
    out.pieces = last_pieces_.data();
    out.piece_count = 1;
    out.pts = last_pts_;
    out.key = false;
    return true;
}

void IngenicAudioStream::releaseFrame(AudioEncodedFrame& out) {
    out.pieces = nullptr;
    out.piece_count = 0;
}

bool IngenicAudioStream::getInfo(AudioStreamInfo& info) {
    std::lock_guard<std::mutex> lock(mtx_);
    info.index = chn_id_;
    info.enabled = started_ ? 1 : 0;
    info.device_index = dev_id_;
    info.channel_index = chn_id_;
    info.sample_rate = actual_sample_rate_;
    info.channels = actual_channels_;
    info.bit_width = actual_bit_width_;
    info.payload = cfg_.payload;
    return true;
}

bool IngenicAudioStream::getCodecConfig(void*& data, size_t& size) {
    if (cfg_.payload != AudioPayloadType::AAC) return false;
    if (aac_dsi_.empty()) return false;
    data = aac_dsi_.data();
    size = aac_dsi_.size();
    return true;
}

bool IngenicAudioStream::openAacEncoder() {
    closeAacEncoder();
    int channels = cfg_.channels;
    aac_handle_ = faacEncOpen(cfg_.sample_rate, channels, &aac_input_samples_, &aac_max_output_bytes_);
    if (!aac_handle_) return false;
    faacEncConfigurationPtr c = faacEncGetCurrentConfiguration(aac_handle_);
    c->mpegVersion = MPEG4;
    c->aacObjectType = LOW;
    c->outputFormat = 1;
    c->inputFormat = FAAC_INPUT_32BIT;
    c->allowMidside = 0;
    c->useLfe = 0;
    c->pnslevel = 0;
    c->jointmode = JOINT_NONE;
    c->bandWidth = cfg_.sample_rate / 2;
    c->quantqual = 100;
    unsigned long br = cfg_.bitrate_per_channel;
    if (br < 32000) br = 32000;
    if (br > 256000) br = 256000;
    c->bitRate = br;
    if (!faacEncSetConfiguration(aac_handle_, c)) return false;
    unsigned char* decInfo = nullptr;
    unsigned long decInfoLen = 0;
    if (faacEncGetDecoderSpecificInfo(aac_handle_, &decInfo, &decInfoLen) == 0 && decInfo && decInfoLen > 0) {
        aac_dsi_.assign(decInfo, decInfo + decInfoLen);
        free(decInfo);
    }
    return true;
}

void IngenicAudioStream::closeAacEncoder() {
    if (aac_handle_) {
        faacEncClose(aac_handle_);
        aac_handle_ = nullptr;
    }
    aac_input_samples_ = 0;
    aac_max_output_bytes_ = 0;
    aac_dsi_.clear();
}

bool IngenicAudioStream::encodeAac(const IMPAudioFrame& frm, std::vector<uint8_t>& out) {
    if (!aac_handle_) return false;
    size_t bytes = frm.len;
    size_t samples = bytes / 2;
    const int16_t* pcm = reinterpret_cast<const int16_t*>(frm.virAddr);
    static std::vector<int16_t> pcm_buf;
    pcm_buf.insert(pcm_buf.end(), pcm, pcm + samples);
    size_t need_per_ch = (size_t)aac_input_samples_;
    size_t ch = (size_t)actual_channels_;
    size_t need_total = need_per_ch * ch;
    if (pcm_buf.size() < need_total) return false;
    static std::vector<int32_t> in32;
    in32.resize(need_total);
    for (size_t i = 0; i < need_total; ++i) {
        int16_t s16 = pcm_buf[i];
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        uint16_t u = static_cast<uint16_t>(s16);
        u = static_cast<uint16_t>((u << 8) | (u >> 8));
        s16 = static_cast<int16_t>(u);
#elif defined(__BIG_ENDIAN__)
        uint16_t u = static_cast<uint16_t>(s16);
        u = static_cast<uint16_t>((u << 8) | (u >> 8));
        s16 = static_cast<int16_t>(u);
#endif
        in32[i] = ((int32_t)s16) << 8;
    }
    pcm_buf.erase(pcm_buf.begin(), pcm_buf.begin() + need_total);
    out.resize(aac_max_output_bytes_);
    int n = faacEncEncode(aac_handle_, in32.data(), (unsigned int)need_per_ch, out.data(), (unsigned int)out.size());
    if (n <= 0) { out.clear(); return false; }
    out.resize(n);
    return true;
}

bool IngenicAudioStream::encodeG711A(const IMPAudioFrame& frm, std::vector<uint8_t>& out) {
    size_t bytes = frm.len;
    size_t samples = bytes / 2;
    const int16_t* pcm = reinterpret_cast<const int16_t*>(frm.virAddr);
    out.resize(samples);
    for (size_t i = 0; i < samples; ++i) out[i] = linear_to_alaw(pcm[i]);
    return true;
}

bool IngenicAudioStream::encodeG711U(const IMPAudioFrame& frm, std::vector<uint8_t>& out) {
    size_t bytes = frm.len;
    size_t samples = bytes / 2;
    const int16_t* pcm = reinterpret_cast<const int16_t*>(frm.virAddr);
    out.resize(samples);
    for (size_t i = 0; i < samples; ++i) out[i] = linear_to_ulaw(pcm[i]);
    return true;
}

uint8_t IngenicAudioStream::linear_to_alaw(int16_t pcm_val) {
    const int16_t ALAW_MAX = 0x7FF;
    const int16_t QUANT_MASK = 0xF;
    const int16_t SEG_SHIFT = 4;
    const int16_t SEG_MASK = 0x70;
    int16_t mask;
    int16_t seg;
    uint8_t aval;
    if (pcm_val >= 0) {
        mask = 0xD5;
    } else {
        mask = 0x55;
        pcm_val = -pcm_val - 1;
    }
    if (pcm_val > ALAW_MAX) pcm_val = ALAW_MAX;
    if (pcm_val >= 256) {
        seg = 7;
        int16_t v = pcm_val >> 8;
        while ((v & 0x80) == 0 && seg > 0) { seg--; v <<= 1; }
    } else {
        seg = (pcm_val >> 4) & 0x7;
    }
    aval = (uint8_t)(seg << SEG_SHIFT);
    aval |= (pcm_val >> (seg + 3)) & QUANT_MASK;
    return aval ^ mask;
}

uint8_t IngenicAudioStream::linear_to_ulaw(int16_t pcm_val) {
    const int16_t CLIP = 8159;
    const int16_t BIAS = 0x84;
    int16_t sign = (pcm_val >> 8) & 0x80;
    if (sign != 0) pcm_val = -pcm_val;
    if (pcm_val > CLIP) pcm_val = CLIP;
    pcm_val = pcm_val + BIAS;
    int16_t exponent = 7;
    for (int expMask = 0x4000; (pcm_val & expMask) == 0 && exponent > 0; expMask >>= 1) exponent--;
    int16_t mantissa = (pcm_val >> (exponent + 3)) & 0x0F;
    uint8_t ulaw = ~(sign | (exponent << 4) | mantissa);
    return ulaw;
}

IngenicAudio::IngenicAudio() {}
IngenicAudio::~IngenicAudio() {}
bool IngenicAudio::init() { return true; }
bool IngenicAudio::exit() { return true; }
std::shared_ptr<IAudioStream> IngenicAudio::createAudioStream() { return std::make_shared<IngenicAudioStream>(); }

} // namespace hal
