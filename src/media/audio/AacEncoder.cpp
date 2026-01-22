#include "AacEncoder.h"

#include <faac.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "Logger.h"

namespace media {

namespace {

constexpr unsigned int kImpMaxAacOutBytes = 8192;

struct AacEncoderContext {
    faacEncHandle encoder = nullptr;
    unsigned long inputSamples = 0;
    unsigned long maxOutputBytes = 0;
    unsigned int numChannels = 0;
    std::vector<int16_t> pcm;
    size_t pcmReadPos = 0;
    std::vector<int32_t> in32;
};

}  // namespace

AacEncoder::~AacEncoder() {
    close();
}

AacEncoder::QualityProfile AacEncoderDefaultProfileFromBitRate(unsigned long bitRatePerChannel) {
    (void)bitRatePerChannel;
    return AacEncoder::QualityProfile::VOICE;
}

unsigned long AacEncoder::adjustBitRateForProfile(unsigned long baseBitRatePerChannel, QualityProfile profile) const {
    if (baseBitRatePerChannel == 0) {
        baseBitRatePerChannel = 64000;
    }
    unsigned long bitRate = baseBitRatePerChannel;
    if (profile == QualityProfile::ENVIRONMENT) {
        unsigned long scaled = baseBitRatePerChannel * 3 / 2;
        if (scaled > bitRate) {
            bitRate = scaled;
        }
    } else if (profile == QualityProfile::MUSIC_HIGH) {
        unsigned long scaled = baseBitRatePerChannel * 2;
        if (scaled > bitRate) {
            bitRate = scaled;
        }
    }
    if (bitRate < 16000) {
        bitRate = 16000;
    }
    return bitRate;
}

int AacEncoder::createContext(const AacEncoderAttr& attr) {
    if (attr.sampleRate == 0 || attr.numChannels == 0) {
        Logger::log(LogLevel::ERROR, "[AacEncoder] open invalid attr\n");
        return -1;
    }

    auto* ctx = new AacEncoderContext();
    ctx->numChannels = attr.numChannels;

    ctx->encoder = faacEncOpen(attr.sampleRate, attr.numChannels, &ctx->inputSamples, &ctx->maxOutputBytes);
    if (!ctx->encoder) {
        Logger::log(LogLevel::ERROR, "[AacEncoder] faacEncOpen failed\n");
        delete ctx;
        return -1;
    }
    
    ctx->pcm.reserve(static_cast<size_t>(ctx->inputSamples) * ctx->numChannels * 4);
    ctx->in32.resize(static_cast<size_t>(ctx->inputSamples) * ctx->numChannels, 0);

    if (ctx->maxOutputBytes > kImpMaxAacOutBytes) {
        Logger::log(
            LogLevel::ERROR,
            "[AacEncoder] maxOutputBytes too large: %lu > %u\n",
            ctx->maxOutputBytes,
            kImpMaxAacOutBytes
        );
        faacEncClose(ctx->encoder);
        delete ctx;
        return -1;
    }

    faacEncConfigurationPtr config = faacEncGetCurrentConfiguration(ctx->encoder);
    if (!config) {
        faacEncClose(ctx->encoder);
        delete ctx;
        return -1;
    }

    config->mpegVersion = MPEG4;
    config->aacObjectType = LOW;
    config->inputFormat = FAAC_INPUT_32BIT;
    config->outputFormat = ADTS_STREAM;
    config->useTns = 0;
    config->useLfe = 0;
    config->jointmode = JOINT_NONE;
    config->pnslevel = 0;
    config->bandWidth = static_cast<unsigned int>(attr.sampleRate / 2);
    config->quantqual = 100;
    unsigned long perChannelBitRate = attr.bitRatePerChannel;
    if (perChannelBitRate < 32000) {
        perChannelBitRate = 32000;
    }
    if (perChannelBitRate > 256000) {
        perChannelBitRate = 256000;
    }
    config->bitRate = perChannelBitRate;
    

    if (!faacEncSetConfiguration(ctx->encoder, config)) {
        faacEncClose(ctx->encoder);
        delete ctx;
        return -1;
    }

    ctx_ = ctx;
    return 0;
}

void AacEncoder::destroyContext() {
    auto* ctx = static_cast<AacEncoderContext*>(ctx_);
    if (!ctx) {
        return;
    }
    if (ctx->encoder) {
        faacEncClose(ctx->encoder);
        ctx->encoder = nullptr;
    }
    delete ctx;
    ctx_ = nullptr;
}

int AacEncoder::encodeFrame(
    const int16_t* pcmData,
    size_t pcmSamples,
    unsigned char* outbuf,
    int outCapacity,
    int* outLen
) {
    auto* ctx = static_cast<AacEncoderContext*>(ctx_);
    if (!ctx || !ctx->encoder || ctx->numChannels == 0 || ctx->inputSamples == 0) {
        return -1;
    }
    if (!pcmData || pcmSamples == 0 || !outbuf || !outLen || outCapacity <= 0) {
        return -1;
    }

    *outLen = 0;

    ctx->pcm.insert(ctx->pcm.end(), pcmData, pcmData + pcmSamples);

    unsigned int capacity = static_cast<unsigned int>(outCapacity);
    if (capacity > kImpMaxAacOutBytes) {
        capacity = kImpMaxAacOutBytes;
    }
    if (capacity > ctx->maxOutputBytes) {
        capacity = static_cast<unsigned int>(ctx->maxOutputBytes);
    }

    const size_t neededSamples = static_cast<size_t>(ctx->inputSamples);
    const size_t neededTotalSamples = neededSamples * static_cast<size_t>(ctx->numChannels);
    if (ctx->pcm.size() < ctx->pcmReadPos + neededTotalSamples) {
        *outLen = 0;
        return 0;
    }

    if (ctx->in32.size() < neededTotalSamples) {
        ctx->in32.resize(neededTotalSamples, 0);
    }
    for (size_t i = 0; i < neededTotalSamples; ++i) {
        int16_t s16 = ctx->pcm[ctx->pcmReadPos + i];
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
        uint16_t u = static_cast<uint16_t>(s16);
        u = static_cast<uint16_t>((u << 8) | (u >> 8));
        s16 = static_cast<int16_t>(u);
#elif defined(__BIG_ENDIAN__)
        uint16_t u = static_cast<uint16_t>(s16);
        u = static_cast<uint16_t>((u << 8) | (u >> 8));
        s16 = static_cast<int16_t>(u);
#endif
        ctx->in32[i] = static_cast<int32_t>(s16) << 8;
    }
    
    int bytes = faacEncEncode(
        ctx->encoder,
        reinterpret_cast<int32_t*>(ctx->in32.data()),
        ctx->inputSamples,
        outbuf,
        static_cast<unsigned int>(capacity)
    );

    if (bytes < 0) {
        Logger::log(LogLevel::ERROR, "[AacEncoder] faacEncEncode error: %d", bytes);
        return -1;
    }

    *outLen = bytes;
    ctx->pcmReadPos += neededTotalSamples;
    if (ctx->pcmReadPos >= neededTotalSamples * 8) {
        ctx->pcm.erase(ctx->pcm.begin(), ctx->pcm.begin() + ctx->pcmReadPos);
        ctx->pcmReadPos = 0;
    }
    return 0;
}

int AacEncoder::open(int sampleRate, int numChannels, unsigned long bitRatePerChannel) {
    close();

    AacEncoderAttr attr{};
    attr.sampleRate = static_cast<unsigned long>(sampleRate);
    attr.numChannels = static_cast<unsigned int>(numChannels);
    attr.bitRatePerChannel = bitRatePerChannel;

    return createContext(attr);
}

int AacEncoder::openWithProfile(
    int sampleRate,
    int numChannels,
    unsigned long baseBitRatePerChannel,
    QualityProfile profile
) {
    unsigned long bitRatePerChannel = adjustBitRateForProfile(baseBitRatePerChannel, profile);
    return open(sampleRate, numChannels, bitRatePerChannel);
}

int AacEncoder::openWithProfileBitWidth(
    int sampleRate,
    int numChannels,
    unsigned long baseBitRatePerChannel,
    QualityProfile profile,
    int sampleBits
) {
    (void)sampleBits;
    close();
    unsigned long bitRatePerChannel = adjustBitRateForProfile(baseBitRatePerChannel, profile);
    AacEncoderAttr attr{};
    attr.sampleRate = static_cast<unsigned long>(sampleRate);
    attr.numChannels = static_cast<unsigned int>(numChannels);
    attr.bitRatePerChannel = bitRatePerChannel;
    return createContext(attr);
}

void AacEncoder::close() {
    destroyContext();
}

void* AacEncoder::context() const {
    return ctx_;
}

int AacEncoder::encode(const int16_t* pcmData, int numSamples, unsigned char* outbuf, int outCapacity, int* outLen) {
    if (!ctx_ || !pcmData || numSamples <= 0) {
        return -1;
    }
    return encodeFrame(pcmData, static_cast<size_t>(numSamples), outbuf, outCapacity, outLen);
}

}  // namespace media
