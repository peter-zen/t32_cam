 #pragma once
#include <cstdint>
#include <cstddef>
 #include <string>
 #include <vector>
 #include <memory>
 
 namespace hal {
 
// 视频编码负载类型，表示输出码流的编码格式
 enum class VideoPayloadType {
     H264,
     H265,
     JPEG
 };
 
// 码率控制模式，非 JPEG 流适用
 enum class VideoRcMode {
     FIXQP,
     CBR,
     VBR,
     CVBR,
     AVBR,
     SMART
 };
 
// 视频通道信息摘要，用于描述当前通道的基础属性
 struct VideoStreamInfo {
     int index;
     int enabled;
     int sensor_index;
     int output_index;
     int width;
     int height;
     int fps_num;
     int fps_den;
     VideoPayloadType payload;
     bool ae_converged;
 };
 
// 编码后的视频帧数据，data 为完整一帧码流（如 JPEG 图片）
struct VideoEncodedPiece {
    void* data;
    size_t size;
};
struct VideoEncodedFrame {
    VideoEncodedPiece* pieces;
    int piece_count;
    uint64_t pts;
    bool key;
};
 
 struct VideoChannelId {
     int sensor_index;
     int stream_index;
 };
 
// VideoStreamConfig 参数详解：
// - payload: 编码负载类型（H264/H265/JPEG）；决定编码器类型与部分通道映射（JPEG 走独立通道并固定为 FIXQP 模式）。
 // - channel: 由 sensor_index 与 stream_index 组成的选择器；组ID=sensor_index*3+stream_index。
// - width: 输出图像宽度（像素），用于设置编码器 picWidth；JPEG 时≤0将使用默认 1920。
// - height: 输出图像高度（像素），用于设置编码器 picHeight 与内部缓冲大小；JPEG 时≤0将使用默认 1080。
// - fps_num: 输出帧率分子，配合 fps_den 组成目标帧率；用于编码器 outFrmRate 与默认 GOP 计算。
// - fps_den: 输出帧率分母；通常为 1。
// - quality: 质量参数；JPEG/FIXQP 模式下映射为 QP（未指定时 JPEG 约 40、FIXQP 约 35）。
// - bitrate: 目标码率（单位 Kbps）；CBR/VBR/SMART/CVBR/AVBR 模式用于设置最大/平均码率，未指定时按分辨率给出合理默认值。
// - profile: 编码 Profile；>0 时生效，否则使用平台默认（H.264/H.265 具体取值由 SDK 决定）。
// - gop: 最大 GOP 长度；<=0 时按 2*fps 自动设置。
// - rc_mode: 码率控制模式；非 JPEG 支持 FIXQP/CBR/VBR/CVBR/AVBR/SMART，决定对应 RC 属性结构。
// - skip_m: 高级跳帧参数 m（IMP_Encoder_STYPE_N1X），默认 3。
// - skip_n: 高级跳帧参数 n（IMP_Encoder_STYPE_N1X），默认 4。
// - enable_ivdc: 是否使能 IVDC，映射到 chn_attr.bEnableIvdc。
// 创建/配置编码通道时的参数
 struct VideoStreamConfig {
     VideoPayloadType payload;
     VideoChannelId channel;
     int width;
     int height;
     int fps_num;
     int fps_den;
     int quality;
     int bitrate;
     int profile;
     int gop;
     VideoRcMode rc_mode;
     int skip_m;
     int skip_n;
     bool enable_ivdc;
 };

 enum class ISPDaynightMode {
    DAY,
    NIGHT
 };

 class IVideoControl {
 public:
    virtual ~IVideoControl() {}
    virtual bool getISPMode(ISPDaynightMode& state) = 0;   
    virtual bool setISPMode(ISPDaynightMode state) = 0;
 };

// 单一路视频流的抽象接口
 class IVideoStream {
 public:
     virtual ~IVideoStream() {}
     // 根据传入的配置创建并绑定编码通道；需在 start 之前调用
     virtual bool configure(const VideoStreamConfig& cfg) = 0;
     // 开始接收编码帧（如 JPEG 快照）
     virtual bool start() = 0;
     // 停止接收并释放编码通道
     virtual bool stop() = 0;
     // 轮询当前通道是否有可读的编码帧，timeout_ms 为毫秒超时
     virtual bool polling(int timeout_ms) = 0;
     // 读取一帧编码数据到 out；读取成功返回 true
     virtual bool getFrame(VideoEncodedFrame& out) = 0;
     // 释放 getFrame 获取的资源（如需）
     virtual void releaseFrame(VideoEncodedFrame& out) = 0;
     virtual bool getInfo(VideoStreamInfo& info) = 0;
 };
 
// 视频子系统抽象接口，负责底层传感器、帧源与编码资源的生命周期
 class IVideo {
 public:
    virtual ~IVideo() {}
    // 初始化底层设备、传感器与帧源（创建并设置属性）
    virtual bool init() = 0;
    // 逆初始化并释放所有资源
    virtual bool exit() = 0;
    // 创建一个视频流对象；由调用方配置并使用
    virtual std::shared_ptr<IVideoStream> createVideoStream() = 0;
 };
 } // namespace hal
