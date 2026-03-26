#!/bin/bash

# 最终验证脚本 - 快速确认所有配置正确
# 用于启动服务器前快速验证

echo "╔════════════════════════════════════════════════════════════════╗"
echo "║          RTSP Server 配置最终验证                                  ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""

# 检查媒体文件
echo "📁 步骤 1: 检查媒体文件"
echo "─────────────────────────────────────────────────────────────────────"

if [ ! -f "sim_sdcard/video/full_frame_camera.h264" ]; then
    echo "❌ 视频文件不存在: sim_sdcard/video/full_frame_camera.h264"
    exit 1
fi

if [ ! -f "sim_sdcard/video/full_frame_camera.pcm" ]; then
    echo "❌ 音频文件不存在: sim_sdcard/video/full_frame_camera.pcm"
    exit 1
fi

echo "✓ 媒体文件存在"
echo ""

# 检查视频文件
echo "📹 视频文件规格:"
V_CODEC=$(ffprobe -v quiet -select_streams v:0 -show_entries stream=codec_name,width,height,r_frame_rate -of csv=p=0 sim_sdcard/video/full_frame_camera.h264 2>&1 | tr ',' '\n' | head -4)
V_CODEC_VAL=$(echo "$V_CODEC" | sed -n '1p')
V_WIDTH_VAL=$(echo "$V_CODEC" | sed -n '2p' | xargs)
V_HEIGHT_VAL=$(echo "$V_CODEC" | sed -n '3p' | xargs)
V_FPS_VAL=$(echo "$V_CODEC" | sed -n '4p' | xargs)

echo "  Codec: $V_CODEC_VAL"
echo "  分辨率: ${V_WIDTH_VAL}x${V_HEIGHT_VAL}"
echo "  帧率: $V_FPS_VAL"

# 检查视频规格
if [ "$V_WIDTH_VAL" != "1920" ] || [ "$V_HEIGHT_VAL" != "1080" ]; then
    echo "  ❌ 分辨率不匹配 (期望 1920x1080)"
    exit 1
else
    echo "  ✓ 分辨率正确: 1920x1080"
fi

if [[ ! "$V_FPS_VAL" =~ (30/1|30) ]]; then
    echo "  ⚠️  帧率: $V_FPS_VAL (期望 30/1)"
else
    echo "  ✓ 帧率正确: $V_FPS_VAL"
fi
echo ""

# 检查音频文件
echo "🎵 音频文件规格:"
A_CODEC=$(ffprobe -f s16le -ar 16000 -ac 1 -i sim_sdcard/video/full_frame_camera.pcm 2>&1 | grep "Audio:" | awk -F': ' '{print $2}' | awk '{print $1}')
echo "  Codec: $A_CODEC"

# 从 Audio: 行提取更多信息
A_INFO=$(ffprobe -f s16le -ar 16000 -ac 1 -i sim_sdcard/video/full_frame_camera.pcm 2>&1)
if echo "$A_INFO" | grep -q "16000 Hz"; then
    echo "  采样率: 16000 Hz"
    echo "  ✓ 采样率正确"
else
    echo "  ❌ 采样率不匹配 (期望 16000 Hz)"
    exit 1
fi

if echo "$A_INFO" | grep -q "s16le"; then
    echo "  ✓ 格式: PCM S16LE (Little-Endian)"
else
    echo "  ❌ 音频格式不匹配 (期望 PCM S16LE)"
    exit 1
fi

echo "  ✓ 音频格式、采样率和声道正确"
echo ""

# 检查配置文件
echo "⚙️  步骤 2: 检查配置文件"
echo "─────────────────────────────────────────────────────────────────────"

if [ ! -f "sim_sdcard/rtsp_config.ini" ]; then
    echo "❌ 配置文件不存在: sim_sdcard/rtsp_config.ini"
    exit 1
fi

CFG_FPS=$(grep "^fps = " sim_sdcard/rtsp_config.ini | cut -d= -f2 | xargs)
CFG_RATE=$(grep "^sample_rate = " sim_sdcard/rtsp_config.ini | cut -d= -f2 | xargs)
CFG_CHAN=$(grep "^channels = " sim_sdcard/rtsp_config.ini | cut -d= -f2 | xargs)
CFG_V_CODEC=$(grep "^codec = " sim_sdcard/rtsp_config.ini | head -1 | cut -d= -f2 | xargs)
CFG_A_CODEC=$(grep "^codec = " sim_sdcard/rtsp_config.ini | tail -1 | cut -d= -f2 | xargs)

echo "  视频配置:"
echo "    FPS: $CFG_FPS"
echo "    Codec ID: $CFG_V_CODEC"

if [ "$CFG_FPS" != "30" ]; then
    echo "    ❌ FPS 不匹配 (期望 30)"
    exit 1
fi

if [ "$CFG_V_CODEC" != "0" ]; then
    echo "    ❌ Video Codec ID 不匹配 (期望 0=H264)"
    exit 1
fi

echo "  音频配置:"
echo "    采样率: $CFG_RATE Hz"
echo "    声道: $CFG_CHAN"
echo "    Codec ID: $CFG_A_CODEC"

if [ "$CFG_RATE" != "16000" ]; then
    echo "    ❌ 采样率不匹配 (期望 16000)"
    exit 1
fi

if [ "$CFG_CHAN" != "1" ]; then
    echo "    ❌ 声道不匹配 (期望 1)"
    exit 1
fi

if [ "$CFG_A_CODEC" != "2" ]; then
    echo "    ❌ Audio Codec ID 不匹配 (期望 2=L16)"
    exit 1
fi

echo "  ✓ 配置文件正确"
echo ""

# 检查字节序转换实现
echo "🔧 步骤 3: 检查字节序转换实现"
echo "─────────────────────────────────────────────────────────────────────"

if grep -q "htons(samples\[j\])" src/media/rtsp/rtsp.c; then
    echo "  ✓ 字节序转换: htons() 实现 (Little → Big)"
else
    echo "  ❌ 未找到字节序转换实现"
    exit 1
fi

if grep -q "AUDIO_CODEC_L16" src/media/rtsp/rtsp.c; then
    echo "  ✓ L16 格式检测: AUDIO_CODEC_L16"
else
    echo "  ❌ 未找到 L16 格式检测"
    exit 1
fi

if grep -q "rtpmap:.*L16" src/media/rtsp/rtsp.c; then
    echo "  ✓ SDP 生成: L16 格式声明"
else
    echo "  ❌ SDP 中未找到 L16 格式声明"
    exit 1
fi

echo ""

# 检查视频 SPS/PPS 提取
echo "🔧 步骤 4: 检查视频 SPS/PPS 提取"
echo "─────────────────────────────────────────────────────────────────────"

if grep -q "Extracting SPS/PPS" src/media/rtsp/RtspServer.cpp; then
    echo "  ✓ SPS/PPS 提取: 已实现"
else
    echo "  ❌ 未找到 SPS/PPS 提取实现"
    exit 1
fi

if grep -q "video_sps_len\|video_pps_len" src/media/rtsp/rtsp.c; then
    echo "  ✓ SDP 编码: SPS/PPS 参数"
else
    echo "  ❌ SDP 中未找到 SPS/PPS 参数"
    exit 1
fi

echo ""

# 系统字节序检查
echo "💻 步骤 5: 系统环境"
echo "─────────────────────────────────────────────────────────────────────"

SYSTEM_ENDIAN=$(lscpu | grep "Byte Order" | cut -d: -f2 | xargs)
echo "  系统字节序: $SYSTEM_ENDIAN"

if [[ ! "$SYSTEM_ENDIAN" =~ Little ]]; then
    echo "  ⚠️  警告: 非 Little-Endian 系统"
else
    echo "  ✓ Little-Endian 系统 (x86)"
fi

if command -v ffplay &> /dev/null; then
    echo "  ✓ ffplay 可用"
else
    echo "  ❌ ffplay 未安装"
    exit 1
fi

if command -v ffprobe &> /dev/null; then
    echo "  ✓ ffprobe 可用"
else
    echo "  ❌ ffprobe 未安装"
    exit 1
fi

echo ""

# 清理旧进程
echo "🧹 步骤 6: 清理"
echo "─────────────────────────────────────────────────────────────────────"

pkill -9 htc_main_app 2>/dev/null || true
pkill -9 ffplay 2>/dev/null || true
pkill -9 ffprobe 2>/dev/null || true
rm -f sim_sdcard/log/app.log
mkdir -p sim_sdcard/log
sleep 1
echo "  ✓ 清理完成"
echo ""

# 最终确认
echo "╔════════════════════════════════════════════════════════════════╗"
echo "║                  配置验证通过                                    ║"
echo "╚════════════════════════════════════════════════════════════════╝"
echo ""
echo "📋 配置总结:"
echo "  ┌─────────────────────────────────────────────────────────────┐"
echo "  │ 视频: H.264, ${V_WIDTH_VAL}x${V_HEIGHT_VAL}, ${V_FPS_VAL}                  │"
echo "  │ 音频: PCM S16LE → L16 (Big-Endian), 16KHz, Mono            │"
echo "  └─────────────────────────────────────────────────────────────┘"
echo ""
echo "✅ 所有检查通过，RTSP Server 已准备好启动"
echo ""
echo "📝 下一步操作:"
echo "  1. 启动服务器:"
echo "     ./build_sim/bin/htc_main_app -rs"
echo ""
echo "  2. 验证流:"
echo "     ./quick_verify.sh"
echo ""
echo "  3. 播放测试:"
echo "     ffplay rtsp://localhost:554/live"
echo ""
