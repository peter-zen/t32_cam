#pragma once
#include "IAudio.h"
#include <imp/imp_audio.h>
#include <imp/imp_dmic.h>
#include <vector>
#include <memory>
#include <mutex>

namespace hal {

class IngenicAudioStream : public IAudioStream {
public:
    IngenicAudioStream();
    ~IngenicAudioStream() override;
    bool configure(const AudioStreamConfig& cfg) override;
    bool start() override;
    bool stop() override;
    bool polling(int timeout_ms) override;
    bool getFrame(AudioEncodedFrame& out) override;
    void releaseFrame(AudioEncodedFrame& out) override;
    bool getInfo(AudioStreamInfo& info) override;
    bool getCodecConfig(void*& data, size_t& size) override;
private:
    bool openAacEncoder();
    void closeAacEncoder();
    bool encodeAac(const IMPAudioFrame& frm, std::vector<uint8_t>& out);
    bool encodeG711A(const IMPAudioFrame& frm, std::vector<uint8_t>& out);
    bool encodeG711U(const IMPAudioFrame& frm, std::vector<uint8_t>& out);
    static uint8_t linear_to_alaw(int16_t pcm_val);
    static uint8_t linear_to_ulaw(int16_t pcm_val);
private:
    AudioStreamConfig cfg_;
    bool configured_;
    bool started_;
    int ref_count_;
    std::mutex mtx_;
    int dev_id_;
    int chn_id_;
    bool use_dmic_;
    int actual_channels_;
    int actual_sample_rate_;
    int actual_bit_width_;
    uint64_t last_pts_;
    IMPAudioFrame last_frame_;
    bool last_frame_valid_;
    IMPDmicChnFrame dmic_last_frame_;
    bool dmic_last_frame_valid_;
    std::vector<AudioEncodedPiece> last_pieces_;
    std::vector<uint8_t> last_buffer_;
    void* aac_handle_;
    unsigned long aac_input_samples_;
    unsigned long aac_max_output_bytes_;
    std::vector<uint8_t> aac_dsi_;
};

class IngenicAudio : public IAudio {
public:
    IngenicAudio();
    ~IngenicAudio() override;
    bool init() override;
    bool exit() override;
    std::shared_ptr<IAudioStream> createAudioStream() override;
};

} // namespace hal
