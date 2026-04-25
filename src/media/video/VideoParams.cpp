#include "VideoParams.h"

using namespace media;

VideoParams::VideoParams()
    : width(1920)
    , height(1080)
    , fps(30)
    , bitrate(2000000)  // 默认2Mbps
    , gop(0)
    , codecFormat(VideoCodecFormat::H265)
    , rcMode(VideoRcMode::CBR) {
}

void VideoParams::setResolution(int width, int height) {
    this->width = width;
    this->height = height;
}

void VideoParams::getResolution(int &width, int &height) const {
    width = this->width;
    height = this->height;
}

void VideoParams::setFrameRate(int fps) {
    this->fps = fps;
}

int VideoParams::getFrameRate() const {
    return fps;
}

void VideoParams::setBitrate(int bitrate) {
    this->bitrate = bitrate;
}

int VideoParams::getBitrate() const {
    return bitrate;
}

void VideoParams::setCodecFormat(VideoCodecFormat format) {
    this->codecFormat = format;
}

VideoCodecFormat VideoParams::getCodecFormat() const {
    return codecFormat;
}

void VideoParams::setRcMode(VideoRcMode mode) {
    this->rcMode = mode;
}

VideoRcMode VideoParams::getRcMode() const {
    return rcMode;
}

void VideoParams::setGop(int gop) {
    this->gop = gop;
}

int VideoParams::getGop() const {
    return gop;
}
