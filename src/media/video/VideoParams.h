#ifndef VIDEO_PARAMS_H
#define VIDEO_PARAMS_H

namespace media {

/**
 * 视频编码格式枚举
 */
enum class VideoCodecFormat {
    H264,  // H.264 编码
    H265   // H.265 编码
};

/**
 * 码率控制模式枚举
 */
enum class VideoRcMode {
    FIXQP,  // 固定QP模式
    CBR,    // 恒定比特率模式
    VBR,    // 可变比特率模式
    CVBR,   //  Constrained VBR模式
    AVBR,   //  Adaptive VBR模式
    SMART   // 智能码率控制模式
};

/**
 * 视频参数类
 */
class VideoParams {
public:
    VideoParams();
    VideoParams(const VideoParams& other) = default;
    ~VideoParams() = default;

    /**
     * 设置视频尺寸
     */
    void setResolution(int width, int height);
    void getResolution(int &width, int &height) const;

    /**
     * 设置视频帧率
     */
    void setFrameRate(int fps);
    int getFrameRate() const;

    /**
     * 设置视频比特率
     */
    void setBitrate(int bitrate);
    int getBitrate() const;

    /**
     * 设置视频编码格式
     */
    void setCodecFormat(VideoCodecFormat format);
    VideoCodecFormat getCodecFormat() const;

    /**
     * 设置码率控制模式
     */
    void setRcMode(VideoRcMode mode);
    VideoRcMode getRcMode() const;

    /**
     * 设置 GOP 帧数；<=0 时由录像器按帧率选择默认值。
     */
    void setGop(int gop);
    int getGop() const;

private:
    int width;
    int height;
    int fps;
    int bitrate;
    int gop;
    VideoCodecFormat codecFormat;
    VideoRcMode rcMode;
};

}  // namespace media

#endif  // VIDEO_PARAMS_H
