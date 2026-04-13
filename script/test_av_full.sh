#!/bin/bash

# RTSP 服务器音视频完整测试脚本
# 使用 ffprobe 和 ffplay headless 模式测试音视频是否正常

set -e  # 遇到错误立即退出

echo "========================================="
echo "  RTSP Server Audio/Video Test Script"
echo "========================================="
echo ""

# 检查必要工具
echo "Checking required tools..."
if ! command -v ffprobe &> /dev/null; then
    echo "✗ ffprobe not found. Installing..."
    bash install_ffplay.sh
fi

if ! command -v ffplay &> /dev/null; then
    echo "✗ ffplay not found. Installing..."
    bash install_ffplay.sh
fi

echo "✓ All required tools available"
echo ""

# 清理旧的日志和进程
echo "Cleaning up..."
mkdir -p sim_sdcard_runtime/logs
rm -f sim_sdcard_runtime/logs/app.log
pkill -9 htc_main_app 2>/dev/null || true
pkill -9 ffprobe 2>/dev/null || true
pkill -9 ffplay 2>/dev/null || true
sleep 1
echo ""

# 初始化模拟环境
echo "Initializing simulation environment..."
mkdir -p sim_sdcard_runtime/configs
mkdir -p sim_sdcard_runtime/media/audio
mkdir -p sim_sdcard_runtime/media/video
mkdir -p sim_sdcard_runtime/data/db

# 复制默认配置
if [ -f res/config.sim.ini ]; then
    cp res/config.sim.ini sim_sdcard_runtime/configs/
fi
if [ -f res/setting.json ]; then
    cp res/setting.json sim_sdcard_runtime/configs/
fi
# 复制 RTSP 模拟配置和资源
if [ -f tests/assets/configs/rtsp_config.ini ]; then
    cp tests/assets/configs/rtsp_config.ini sim_sdcard_runtime/configs/
fi
if [ -d tests/assets/video ]; then
    cp tests/assets/video/* sim_sdcard_runtime/media/video/
fi
if [ -d tests/assets/audio ]; then
    cp tests/assets/audio/* sim_sdcard_runtime/media/audio/
fi
echo "✓ Simulation environment ready at sim_sdcard_runtime"
echo ""

# 启动 RTSP 服务器
echo "========================================="
echo "  Step 1: Starting RTSP Server"
echo "========================================="
cd build_sim
./bin/htc_main_app -rs > ../server_output.log 2>&1 &
RTSP_PID=$!
cd ..
echo "✓ RTSP server started (PID: $RTSP_PID)"

# 等待服务器完全启动
echo "Waiting for server to be ready..."
for i in {1..10}; do
    if kill -0 $RTSP_PID 2>/dev/null; then
        echo "  Server running (check $i/10)"
        sleep 1
    else
        echo "✗ Server failed to start"
        cat server_output.log
        exit 1
    fi
done
echo ""

# 测试 1: 使用 ffprobe 检查流信息
echo "========================================="
echo "  Step 2: Analyzing Streams with ffprobe"
echo "========================================="
echo "Running ffprobe analysis..."

ffprobe -v info -show_streams -show_format rtsp://localhost:8554/live 2>&1 > ffprobe_output.log &
FFPROBE_PID=$!

# 等待 ffprobe 收集数据
sleep 5

# 检查 ffprobe 是否还在运行（如果没有数据可能会快速退出）
if kill -0 $FFPROBE_PID 2>/dev/null; then
    echo "ffprobe is still analyzing, waiting more..."
    sleep 3
    kill $FFPROBE_PID 2>/dev/null || true
fi

echo ""
echo "=== ffprobe Analysis Results ==="

# 分析 ffprobe 输出
if [ -f "ffprobe_output.log" ]; then
    echo "✓ ffprobe output collected"

    # 检查是否有视频流
    if grep -q "codec_type=video" ffprobe_output.log; then
        VIDEO_CODEC=$(grep -A 2 "codec_type=video" ffprobe_output.log | grep "codec_name=" | cut -d= -f2 | head -1)
        VIDEO_WIDTH=$(grep -A 10 "codec_type=video" ffprobe_output.log | grep "width=" | cut -d= -f2 | head -1)
        VIDEO_HEIGHT=$(grep -A 10 "codec_type=video" ffprobe_output.log | grep "height=" | cut -d= -f2 | head -1)
        VIDEO_FPS=$(grep -A 10 "codec_type=video" ffprobe_output.log | grep "r_frame_rate=" | cut -d= -f2 | head -1)
        echo "✓ VIDEO STREAM DETECTED:"
        echo "  - Codec: $VIDEO_CODEC"
        echo "  - Resolution: ${VIDEO_WIDTH}x${VIDEO_HEIGHT}"
        echo "  - Frame Rate: $VIDEO_FPS"
    else
        echo "✗ NO VIDEO STREAM DETECTED"
    fi

    echo ""

    # 检查是否有音频流
    if grep -q "codec_type=audio" ffprobe_output.log; then
        AUDIO_CODEC=$(grep -A 2 "codec_type=audio" ffprobe_output.log | grep "codec_name=" | cut -d= -f2 | head -1)
        AUDIO_RATE=$(grep -A 10 "codec_type=audio" ffprobe_output.log | grep "sample_rate=" | cut -d= -f2 | head -1)
        AUDIO_CHANNELS=$(grep -A 10 "codec_type=audio" ffprobe_output.log | grep "channels=" | cut -d= -f2 | head -1)
        echo "✓ AUDIO STREAM DETECTED:"
        echo "  - Codec: $AUDIO_CODEC"
        echo "  - Sample Rate: $AUDIO_RATE Hz"
        echo "  - Channels: $AUDIO_CHANNELS"
    else
        echo "✗ NO AUDIO STREAM DETECTED"
    fi

    echo ""
    echo "Detailed ffprobe output:"
    cat ffprobe_output.log | grep -E "(codec_type|codec_name|width|height|sample_rate|channels|r_frame_rate)" | head -20
else
    echo "✗ ffprobe output not found"
fi
echo ""

# 让服务器产生一些日志
echo "Waiting for server to generate logs..."
sleep 5
echo ""

# 测试 2: 使用 ffplay headless 模式播放测试
echo "========================================="
echo "  Step 3: Playback Test with ffplay"
echo "========================================="
echo "Running ffplay in headless mode (10 seconds)..."

# 使用 ffplay 的 headless 模式（不显示窗口）
# -nodisp: 不显示窗口
# -autoexit: 播放完成后自动退出
# -t 10: 最多播放10秒
# -loglevel info: 显示日志信息
timeout 12s ffplay -nodisp -autoexit -t 10 -loglevel info rtsp://localhost:8554/live > ffplay_output.log 2>&1 &
FFPLAY_PID=$!

echo "ffplay started (PID: $FFPLAY_PID)"

# 监控 ffplay 运行状态
FFPLAY_RUNNING=true
for i in {1..15}; do
    if ! kill -0 $FFPLAY_PID 2>/dev/null; then
        echo "  ffplay finished after $((i/2)) seconds"
        FFPLAY_RUNNING=false
        break
    fi
    sleep 1
done

# 如果 ffplay 还在运行，强制停止
if $FFPLAY_RUNNING; then
    echo "  ffplay still running, stopping..."
    kill $FFPLAY_PID 2>/dev/null || true
    sleep 1
fi

echo ""
echo "=== ffplay Playback Test Results ==="

# 分析 ffplay 输出
if [ -f "ffplay_output.log" ]; then
    echo "✓ ffplay output collected"

    # 检查是否检测到视频
    if grep -q "Video:" ffplay_output.log || grep -q "detected" ffplay_output.log; then
        echo "✓ Video playback detected"
        grep "Video:" ffplay_output.log | head -5
    else
        echo "? Video playback not clearly detected in logs"
    fi

    echo ""

    # 检查是否检测到音频
    if grep -q "Audio:" ffplay_output.log; then
        echo "✓ Audio playback detected"
        grep "Audio:" ffplay_output.log | head -5
    else
        echo "? Audio playback not clearly detected in logs"
    fi

    echo ""

    # 检查是否有错误
    if grep -qi "error" ffplay_output.log; then
        echo "⚠ Errors detected in ffplay output:"
        grep -i "error" ffplay_output.log | head -10
    else
        echo "✓ No errors detected in ffplay playback"
    fi

    echo ""

    # 显示播放统计
    echo "Playback Statistics:"
    grep -E "(frame=|fps=|bitrate=|size=|time=)" ffplay_output.log | tail -1
else
    echo "✗ ffplay output not found"
fi
echo ""

# 测试 3: 分析服务器日志
echo "========================================="
echo "  Step 4: Server Log Analysis"
echo "========================================="

if [ -f "sim_sdcard/log/app.log" ]; then
    echo "✓ Server log found"

    echo ""
    echo "=== Connection Status ==="
    if grep -q "Client connected" sim_sdcard/log/app.log; then
        CONNECTION_COUNT=$(grep -c "Client connected" sim_sdcard/log/app.log)
        echo "✓ Client connections detected: $CONNECTION_COUNT"
    else
        echo "? No client connections detected in logs"
    fi

    echo ""
    echo "=== Video Session Status ==="
    if grep -q "Video session started" sim_sdcard/log/app.log; then
        echo "✓ Video session started"
    else
        echo "✗ Video session not started"
    fi

    if grep -q "Video source opened" sim_sdcard/log/app.log; then
        echo "✓ Video source opened"
    else
        echo "✗ Video source not opened"
    fi

    echo ""
    echo "=== Audio Session Status ==="
    if grep -q "Audio session started" sim_sdcard/log/app.log; then
        echo "✓ Audio session started"
    else
        echo "✗ Audio session not started"
    fi

    if grep -q "Audio source opened" sim_sdcard/log/app.log; then
        echo "✓ Audio source opened"
    else
        echo "✗ Audio source not opened"
    fi

    echo ""
    echo "=== Data Production ==="
    VIDEO_PULLS=$(grep -c "\[FIFO-V\] pull:" sim_sdcard/log/app.log || echo "0")
    AUDIO_PULLS=$(grep -c "\[FIFO-A\] pull:" sim_sdcard/log/app.log || echo "0")
    echo "Video data pulls: $VIDEO_PULLS"
    echo "Audio data pulls: $AUDIO_PULLS"

    if [ $VIDEO_PULLS -gt 0 ] && [ $AUDIO_PULLS -gt 0 ]; then
        echo "✓ Both video and audio data are being produced"
    elif [ $VIDEO_PULLS -gt 0 ]; then
        echo "⚠ Only video data is being produced"
    elif [ $AUDIO_PULLS -gt 0 ]; then
        echo "⚠ Only audio data is being produced"
    else
        echo "✗ No data production detected"
    fi

    echo ""
    echo "=== Error Analysis ==="
    ERROR_COUNT=$(grep -c -i "error" sim_sdcard/log/app.log || echo "0")
    FIFO_EMPTY=$(grep -c "FIFO.*empty" sim_sdcard/log/app.log || echo "0")
    FIFO_FULL=$(grep -c "FIFO.*full" sim_sdcard/log/app.log || echo "0")

    echo "Error count: $ERROR_COUNT"
    echo "FIFO empty count: $FIFO_EMPTY"
    echo "FIFO full count: $FIFO_FULL"

    if [ $ERROR_COUNT -gt 10 ]; then
        echo "⚠ High error count detected"
    fi
else
    echo "✗ Server log not found"
fi
echo ""

# 停止服务器
echo "========================================="
echo "  Step 5: Cleanup"
echo "========================================="
echo "Stopping RTSP server..."
kill -INT $RTSP_PID 2>/dev/null || true

# 等待服务器停止
sleep 2

if kill -0 $RTSP_PID 2>/dev/null; then
    echo "  Server still running, force killing..."
    kill -9 $RTSP_PID 2>/dev/null || true
else
    echo "  ✓ Server stopped gracefully"
fi
echo ""

# 最终总结
echo "========================================="
echo "  FINAL TEST SUMMARY"
echo "========================================="

FINAL_STATUS="✓ PASSED"

# 检查测试结果
HAS_VIDEO=0
HAS_AUDIO=0

if [ -f "ffprobe_output.log" ]; then
    if grep -q "codec_type=video" ffprobe_output.log; then
        HAS_VIDEO=1
    fi
    if grep -q "codec_type=audio" ffprobe_output.log; then
        HAS_AUDIO=1
    fi
fi

echo "Video Stream: $([ $HAS_VIDEO -eq 1 ] && echo "✓ DETECTED" || echo "✗ NOT DETECTED")"
echo "Audio Stream: $([ $HAS_AUDIO -eq 1 ] && echo "✓ DETECTED" || echo "✗ NOT DETECTED")"
echo ""

if [ $HAS_VIDEO -eq 1 ] && [ $HAS_AUDIO -eq 1 ]; then
    echo "========================================="
    echo "  ✓✓✓ ALL TESTS PASSED ✓✓✓"
    echo "========================================="
    echo "Audio and video streams are both working correctly!"
    FINAL_STATUS="PASSED"
else
    echo "========================================="
    echo "  ✗✗✗ TESTS FAILED ✗✗✗"
    echo "========================================="
    FINAL_STATUS="FAILED"
fi

echo ""
echo "Test artifacts saved:"
echo "  - server_output.log"
echo "  - ffprobe_output.log"
echo "  - ffplay_output.log"
echo "  - sim_sdcard_runtime/logs/app.log"
echo ""

exit $([ "$FINAL_STATUS" = "PASSED" ] && echo 0 || echo 1)
