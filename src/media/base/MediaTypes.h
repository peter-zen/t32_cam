/**
 * @file MediaTypes.h
 * @brief 媒体类型定义
 */

#ifndef MEDIA_TYPES_H
#define MEDIA_TYPES_H

#include <cstdint>

namespace media {

enum class MediaType {
    VIDEO = 0,
    AUDIO = 1
};

enum class VideoCodec {
    H264 = 0,
    H265 = 1
};

enum class AudioCodec {
    PCMU = 0,
    PCMA = 1,
    L16 = 2,
    AAC = 3
};

struct MediaParams {
    MediaType type;
    int sampleRate = 16000;
    int channels = 1;
    int bitsPerSample = 16;
    AudioCodec audioCodec = AudioCodec::PCMA;
    VideoCodec videoCodec = VideoCodec::H264;
    int videoWidth = 1920;
    int videoHeight = 1080;
    int videoFrameRate = 25;
    int videoBitRate = 4000000;
};

} 

#endif