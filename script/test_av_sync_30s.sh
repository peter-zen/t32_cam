#!/bin/bash

echo "========================================="
echo "  RTSP客户端AV Sync验证（30秒）"
echo "  日期: 2026-01-18"
echo "========================================="
echo ""

# 清理
pkill -9 htc_main_app 2>/dev/null || true
pkill -9 ffplay 2>/dev/null || true
pkill -9 ffprobe 2>/dev/null || true
sleep 1

# 启动RTSP服务器
echo "步骤1: 启动RTSP服务器..."
./build_sim/bin/htc_main_app -rs > test_av_sync.log 2>&1 &
RTSP_PID=$!
sleep 2
echo "  ✓ RTSP服务器启动 (PID: $RTSP_PID)"
echo ""

# 启动ffprobe持续监控
echo "步骤2: 启动ffprobe监控..."
ffprobe -v info -show_streams -show_frames -show_entries frame=pkt_pts_time,pkt_dts_time,pkt_duration,pkt_pos:stream_index \
    rtsp://localhost:554/live > test_av_sync_client.log 2>&1 &
FFPROBE_PID=$!
echo "  ✓ ffprobe启动 (PID: $FFPROBE_PID)"
echo ""

# 同时启动ffplay播放（可选，用于验证）
echo "步骤3: 启动ffplay播放（10秒）..."
timeout 10s ffplay -nodisp -autoexit -loglevel info rtsp://localhost:554/live > /dev/null 2>&1 &
FFPLAY_PID=$!
echo "  ✓ ffplay启动 (PID: $FFPLAY_PID)"
echo ""

echo "等待10秒ffplay播放..."
sleep 10

# 继续让ffprobe采集30秒数据
echo "步骤4: 继续采集ffprobe数据（30秒）..."
for i in {1..30}; do
    echo -ne "\r  进度: [$i/30] 秒"
    sleep 1
done
echo ""

# 停止所有进程
echo "步骤5: 停止所有进程..."
kill $FFPROBE_PID 2>/dev/null || true
kill $FFPLAY_PID 2>/dev/null || true
wait $FFPROBE_PID 2>/dev/null || true
wait $FFPLAY_PID 2>/dev/null || true
sleep 1
kill $RTSP_PID 2>/dev/null || true
sleep 1
echo "  ✓ 所有进程已停止"
echo ""

echo "步骤6: 分析AV Sync..."
echo ""

# 分析Video帧时间戳
echo "=== Video帧分析 ==="
VIDEO_TS_START=$(grep "^\[frame.*stream_index=1\]" test_av_sync_client.log | head -1 | grep -oP "pts_time=\K[0-9.]+")
VIDEO_TS_END=$(grep "^\[frame.*stream_index=1\]" test_av_sync_client.log | tail -1 | grep -oP "pts_time=\K[0-9.]+")
VIDEO_FRAME_COUNT=$(grep -c "^\[frame.*stream_index=1\]" test_av_sync_client.log)

if [ ! -z "$VIDEO_TS_START" ] && [ ! -z "$VIDEO_TS_END" ] && [ ! -z "$VIDEO_FRAME_COUNT" ]; then
    VIDEO_DURATION_SEC=$(echo "scale=3; ($VIDEO_TS_END - $VIDEO_TS_START) / 1000000000" | bc)

    echo "Video帧数: $VIDEO_FRAME_COUNT"
    echo "Video时长: $VIDEO_DURATION_SEC 秒"
    echo "Video平均FPS: $(echo "scale=2; $VIDEO_FRAME_COUNT / $VIDEO_DURATION_SEC" | bc) fps"
    echo ""
else
    echo "✗ Video数据不足，无法分析"
fi

# 分析Audio包时间戳
echo "=== Audio包分析 ==="
AUDIO_TS_START=$(grep "^\[frame.*stream_index=0\]" test_av_sync_client.log | head -1 | grep -oP "pts_time=\K[0-9.]+")
AUDIO_TS_END=$(grep "^\[frame.*stream_index=0\]" test_av_sync_client.log | tail -1 | grep -oP "pts_time=\K[0-9.]+")
AUDIO_PKT_COUNT=$(grep -c "^\[frame.*stream_index=0\]" test_av_sync_client.log)

if [ ! -z "$AUDIO_TS_START" ] && [ ! -z "$AUDIO_TS_END" ] && [ ! -z "$AUDIO_PKT_COUNT" ]; then
    AUDIO_DURATION_SEC=$(echo "scale=3; ($AUDIO_TS_END - $AUDIO_TS_START) / 1000000000" | bc)
    AUDIO_PPS=$(echo "scale=2; $AUDIO_PKT_COUNT / $AUDIO_DURATION_SEC" | bc)

    echo "Audio包数: $AUDIO_PKT_COUNT"
    echo "Audio时长: $AUDIO_DURATION_SEC 秒"
    echo "Audio平均包率: $AUDIO_PPS pps"
    echo ""
else
    echo "✗ Audio数据不足，无法分析"
fi

# 分析时间戳单调性
echo "=== 时间戳单调性分析 ==="
echo "检查Video pts_time是否单调递增..."
VIDEO_TS_NOT_MONOTONIC=$(grep "^\[frame.*stream_index=1\]" test_av_sync_client.log | \
    grep -oP "pts_time=\K[0-9]+" | \
    awk 'NR>1 && prev > $0 {print "Non-monotonic at frame", NR} {prev=$0}')

if [ -z "$VIDEO_TS_NOT_MONOTONIC" ]; then
    echo "✓ Video pts_time单调递增"
else
    echo "$VIDEO_TS_NOT_MONOTONIC"
    echo "✗ Video pts_time存在非单调性问题"
fi
echo ""

echo "检查Audio pts_time是否单调递增..."
AUDIO_TS_NOT_MONOTONIC=$(grep "^\[frame.*stream_index=0\]" test_av_sync_client.log | \
    grep -oP "pts_time=\K[0-9]+" | \
    awk 'NR>1 && prev > $0 {print "Non-monotonic at frame", NR} {prev=$0}')

if [ -z "$AUDIO_TS_NOT_MONOTONIC" ]; then
    echo "✓ Audio pts_time单调递增"
else
    echo "$AUDIO_TS_NOT_MONOTONIC"
    echo "✗ Audio pts_time存在非单调性问题"
fi
echo ""

# 分析时间戳间隔
echo "=== 时间戳间隔分析 ==="
echo "Video帧间间隔（应该是33333us = 1/30秒）:"
grep "^\[frame.*stream_index=1\]" test_av_sync_client.log | \
    grep -oP "pts_time=\K[0-9]+" | \
    awk 'NR>1 {diff=$1-prev; print diff, int(diff/33333), "x" 33333} {prev=$1}' | \
    head -20

echo ""
echo "Audio包间间隔（应该是20000us = 1/50秒）:"
grep "^\[frame.*stream_index=0\]" test_av_sync_client.log | \
    grep -oP "pts_time=\K[0-9]+" | \
    awk 'NR>1 {diff=$1-prev; print diff, int(diff/20000), "x" 20000} {prev=$1}' | \
    head -20

echo ""

# 分析生产者vs客户端
echo "=== 生产者vs客户端对比 ==="
echo "从服务器日志分析..."
VIDEO_PRODUCED=$(grep "\[VIDEO\]" test_av_sync.log | grep "Frames produced:" | tail -1 | grep -oP 'Frames produced: \K\d+')
AUDIO_PRODUCED=$(grep "\[AUDIO\]" test_av_sync.log | grep "Frames produced:" | tail -1 | grep -oP 'Frames produced: \K\d+')

if [ ! -z "$VIDEO_PRODUCED" ]; then
    echo "Video生产帧数: $VIDEO_PRODUCED"
fi

if [ ! -z "$AUDIO_PRODUCED" ]; then
    echo "Audio生产包数: $AUDIO_PRODUCED"
fi

if [ ! -z "$VIDEO_FRAME_COUNT" ]; then
    echo "Video客户端接收帧数: $VIDEO_FRAME_COUNT"
    if [ ! -z "$VIDEO_PRODUCED" ]; then
        VIDEO_LOSS=$(echo "$VIDEO_PRODUCED - $VIDEO_FRAME_COUNT" | bc)
        echo "Video丢帧率: $(echo "scale=2; $VIDEO_LOSS * 100 / $VIDEO_PRODUCED" | bc)%"
    fi
fi

if [ ! -z "$AUDIO_PKT_COUNT" ]; then
    echo "Audio客户端接收包数: $AUDIO_PKT_COUNT"
    if [ ! -z "$AUDIO_PRODUCED" ]; then
        AUDIO_LOSS=$(echo "$AUDIO_PRODUCED - $AUDIO_PKT_COUNT" | bc)
        echo "Audio丢包率: $(echo "scale=2; $AUDIO_LOSS * 100 / $AUDIO_PRODUCED" | bc)%"
    fi
fi
echo ""

echo "========================================="
echo "  测试完成"
echo "========================================="
echo ""
echo "详细日志:"
echo "  - 服务器端: test_av_sync.log"
echo "  - 客户端: test_av_sync_client.log"
echo ""
echo "客户端日志已包含:"
echo "  - pts_time (显示时间戳，用于AV Sync)"
echo "  - pkt_duration (包持续时间)"
echo "  - stream_index (0=Audio, 1=Video)"
echo ""
