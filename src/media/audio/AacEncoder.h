#ifndef AAC_ENCODER_H
#define AAC_ENCODER_H

#include <stdint.h>
#include <stddef.h>

namespace media {

struct AacEncoderAttr {
    unsigned long sampleRate;
    unsigned int numChannels;
    unsigned long bitRatePerChannel;
};

class AacEncoder {
public:
    AacEncoder() = default;
    ~AacEncoder();
    enum class QualityProfile {
        VOICE,
        ENVIRONMENT,
        MUSIC_HIGH
    };
    int open(int sampleRate, int numChannels, unsigned long bitRatePerChannel);
    int openWithProfile(
        int sampleRate,
        int numChannels,
        unsigned long baseBitRatePerChannel,
        QualityProfile profile
    );
    int openWithProfileBitWidth(
        int sampleRate,
        int numChannels,
        unsigned long baseBitRatePerChannel,
        QualityProfile profile,
        int sampleBits
    );
    void close();
    void* context() const;
    int encode(const int16_t* pcmData, int numSamples, unsigned char* outbuf, int outCapacity, int* outLen);

private:
    int createContext(const AacEncoderAttr& attr);
    void destroyContext();
    int encodeFrame(const int16_t* pcmData, size_t pcmSamples, unsigned char* outbuf, int outCapacity, int* outLen);
    unsigned long adjustBitRateForProfile(unsigned long baseBitRatePerChannel, QualityProfile profile) const;
    void* ctx_ = nullptr;
};

}  // namespace media

#endif  // AAC_ENCODER_H
