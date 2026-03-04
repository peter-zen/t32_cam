#!/bin/bash

echo "========================================="
echo "  RTSP FIFO机制修正验证"
echo "  日期: 2026-01-18"
echo "========================================="
echo ""

# 清理
pkill -9 htc_main_app 2>/dev/null || true
pkill -9 ffplay 2>/dev/null || true
sleep 1

# 启动RTSP服务器
echo "步骤1: 启动RTSP服务器..."
./build_sim/bin/htc_main_app -rs > test_fifo_fixed.log 2>&1 &
RTSP_PID=$!
sleep 2
echo "  ✓ RTSP服务器启动 (PID: $RTSP_PID)"
echo ""

# 测试1: 使用ffplay连接5秒
echo "步骤2: 使用ffplay连接5秒..."
timeout 5s ffplay -nodisp -autoexit -loglevel info rtsp://localhost:8554/live > /dev/null 2>&1 &
FFPLAY_PID=$!

echo "  等待5秒..."
sleep 5

echo ""
echo "步骤3: 分析日志..."
echo ""

# 分析关键指标
echo "=== 连接状态 ==="
CLIENT_COUNT=$(grep -c "Client connected" test_fifo_fixed.log || echo "0")
echo "客户端连接次数: $CLIENT_COUNT"
echo ""

echo "=== Session状态 ==="
VIDEO_SESSION=$(grep -c "Video session started" test_fifo_fixed.log || echo "0")
AUDIO_SESSION=$(grep -c "Audio session started" test_fifo_fixed.log || echo "0")
echo "Video session启动: $VIDEO_SESSION"
echo "Audio session启动: $AUDIO_SESSION"
echo ""

echo "=== 生产者行为检测 ==="
VIDEOFILE_DETECTED=$(grep -c "VideoFileSource detected" test_fifo_fixed.log || echo "0")
AUDIOFILE_DETECTED=$(grep -c "AudioFileSource detected" test_fifo_fixed.log || echo "0")
echo "VideoFileSource检测: $VIDEOFILE_DETECTED"
echo "AudioFileSource检测: $AUDIOFILE_DETECTED"
echo ""

if [ "$VIDEOFILE_DETECTED" -gt 0 ]; then
    echo "Video帧间隔:"
    grep "VideoFileSource detected" test_fifo_fixed.log | tail -1
fi

if [ "$AUDIOFILE_DETECTED" -gt 0 ]; then
    echo "Audio包间隔:"
    grep "AudioFileSource detected" test_fifo_fixed.log | tail -1
fi
echo ""

echo "=== FIFO行为 ==="
FIFO_DROP_OLD=$(grep -c "FIFO full, dropping oldest frame" test_fifo_fixed.log || echo "0")
echo "Drop Old次数: $FIFO_DROP_OLD"
echo ""

echo "=== 生产帧数统计 ==="
FRAMES_VIDEO=$(grep "Frames produced:" test_fifo_fixed.log | grep -i "video" | awk '{print $NF}' || echo "0")
FRAMES_AUDIO=$(grep "Frames produced:" test_fifo_fixed.log | grep -i "audio" | awk '{print $NF}' || echo "0")
echo "Video生产帧数: $FRAMES_VIDEO"
echo "Audio生产帧数: $FRAMES_AUDIO"
echo ""

echo "=== 预期vs实际对比 ==="
# 预期：5秒 * 30fps = 150帧
EXPECTED_VIDEO=150
EXPECTED_AUDIO=250  # 5秒 * 50包/秒 (16kHz / 320 samples)

echo "预期Video帧数: $EXPECTED_VIDEO (5秒 * 30fps)"
echo "实际Video帧数: $FRAMES_VIDEO"
if [ "$FRAMES_VIDEO" -le "$((EXPECTED_VIDEO + 10))" ] && [ "$FRAMES_VIDEO" -ge "$((EXPECTED_VIDEO - 10))" ]; then
    echo "  ✓ Video生产速率正常"
else
    echo "  ✗ Video生产速率异常"
fi
echo ""

echo "预期Audio包数: $EXPECTED_AUDIO (5秒 * 50包/秒)"
echo "实际Audio包数: $FRAMES_AUDIO"
if [ "$FRAMES_AUDIO" -le "$((EXPECTED_AUDIO + 20))" ] && [ "$FRAMES_AUDIO" -ge "$((EXPECTED_AUDIO - 20))" ]; then
    echo "  ✓ Audio生产速率正常"
else
    echo "  ✗ Audio生产速率异常"
fi
echo ""

# 停止服务器
echo "步骤4: 清理..."
kill $RTSP_PID 2>/dev/null || true
sleep 1
echo "  ✓ 停止RTSP服务器"
echo ""

echo "========================================="
echo "  测试完成"
echo "========================================="
echo ""
echo "详细日志: test_fifo_fixed.log"
echo ""

# 显示关键日志片段
echo "=== 关键日志片段 ==="
echo ""
echo "1. 源检测和速率配置:"
grep -E "FileSource detected" test_fifo_fixed.log | tail -5
echo ""

echo "2. Session启动:"
grep -E "session started" test_fifo_fixed.log | tail -5
echo ""

echo "3. FIFO行为:"
grep -E "FIFO" test_fifo_fixed.log | tail -10
echo ""
