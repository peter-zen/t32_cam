#!/bin/bash

echo "========================================="
echo "  RTSP生产者稳定性验证（30秒）"
echo "  日期: 2026-01-18"
echo "========================================="
echo ""

# 清理
pkill -9 htc_main_app 2>/dev/null || true
pkill -9 ffplay 2>/dev/null || true
sleep 1

# 启动RTSP服务器
echo "步骤1: 启动RTSP服务器..."
./build_sim/bin/htc_main_app -rs > test_fifo_30s_final.log 2>&1 &
RTSP_PID=$!
sleep 2
echo "  ✓ RTSP服务器启动 (PID: $RTSP_PID)"
echo ""

# 测试: 使用ffplay连接30秒
echo "步骤2: 使用ffplay连接30秒..."
timeout 30s ffplay -nodisp -autoexit -loglevel warning rtsp://localhost:554/live > /dev/null 2>&1 &
FFPLAY_PID=$!

echo "  等待30秒..."
for i in {1..30}; do
    echo -ne "\r  进度: [$i/30] 秒"
    sleep 1
done
echo ""
echo ""

# 等待ffplay结束
wait $FFPLAY_PID 2>/dev/null || true
sleep 1

echo "步骤3: 分析生产者统计..."
echo ""

# 提取Video统计信息
echo "=== Video Session统计 ==="
VIDEO_LINE=$(grep "\[VIDEO\] MediaSession stopped" test_fifo_30s_final.log)
if [ ! -z "$VIDEO_LINE" ]; then
    VIDEO_FRAMES=$(echo "$VIDEO_LINE" | grep -oP 'Frames produced: \K\d+')
    VIDEO_DURATION=$(echo "$VIDEO_LINE" | grep -oP 'duration: \K[\d.]+')
    VIDEO_RATE=$(echo "$VIDEO_LINE" | grep -oP 'avg rate: \K[\d.]+')

    echo "生产帧数: $VIDEO_FRAMES"
    echo "运行时间: $VIDEO_DURATION 秒"
    echo "平均速率: $VIDEO_RATE fps"
    echo ""

    # 计算预期帧数
    EXPECTED=$(echo "$VIDEO_DURATION * 30" | bc)
    ERROR_PERCENT=$(python3 -c "print(f'{abs($VIDEO_FRAMES - $EXPECTED) / $EXPECTED * 100:.2f}')")

    echo "预期帧数: $EXPECTED (时间×30fps)"
    echo "误差: $ERROR_PERCENT%"
    if (( $(echo "$ERROR_PERCENT < 1.0" | bc -l) )); then
        echo "  ✓ 生产速率非常稳定 (误差<1%)"
    elif (( $(echo "$ERROR_PERCENT < 2.0" | bc -l) )); then
        echo "  ✓ 生产速率稳定 (误差<2%)"
    else
        echo "  ✗ 生产速率不稳定 (误差≥2%)"
    fi
else
    echo "✗ 未找到Video session统计信息"
fi
echo ""

# 提取Audio统计信息
echo "=== Audio Session统计 ==="
AUDIO_LINE=$(grep "\[AUDIO\] MediaSession stopped" test_fifo_30s_final.log)
if [ ! -z "$AUDIO_LINE" ]; then
    AUDIO_PACKETS=$(echo "$AUDIO_LINE" | grep -oP 'Frames produced: \K\d+')
    AUDIO_DURATION=$(echo "$AUDIO_LINE" | grep -oP 'duration: \K[\d.]+')
    AUDIO_RATE=$(echo "$AUDIO_LINE" | grep -oP 'avg rate: \K[\d.]+')

    echo "生产包数: $AUDIO_PACKETS"
    echo "运行时间: $AUDIO_DURATION 秒"
    echo "平均速率: $AUDIO_RATE 包/秒"
    echo ""

    # 计算预期包数
    EXPECTED=$(echo "$AUDIO_DURATION * 50" | bc)
    ERROR_PERCENT=$(python3 -c "print(f'{abs($AUDIO_PACKETS - $EXPECTED) / $EXPECTED * 100:.2f}')")

    echo "预期包数: $EXPECTED (时间×50包/秒)"
    echo "误差: $ERROR_PERCENT%"
    if (( $(echo "$ERROR_PERCENT < 1.0" | bc -l) )); then
        echo "  ✓ 生产速率非常稳定 (误差<1%)"
    elif (( $(echo "$ERROR_PERCENT < 2.0" | bc -l) )); then
        echo "  ✓ 生产速率稳定 (误差<2%)"
    else
        echo "  ✗ 生产速率不稳定 (误差≥2%)"
    fi
else
    echo "✗ 未找到Audio session统计信息"
fi
echo ""

# FIFO Drop Old统计
echo "=== FIFO Drop Old统计 ==="
DROP_COUNT=$(grep -c "FIFO full, dropping oldest frame" test_fifo_30s_final.log || echo "0")
echo "Drop Old次数: $DROP_COUNT"
if [ "$DROP_COUNT" -eq 0 ]; then
    echo "  ✓ FIFO完全正常 (无Drop)"
elif [ "$DROP_COUNT" -lt 50 ]; then
    echo "  ✓ FIFO行为正常 (Drop Old次数较少)"
elif [ "$DROP_COUNT" -lt 200 ]; then
    echo "  ⚠ FIFO Drop Old较多，但可接受"
else
    echo "  ✗ FIFO Drop Old过多，可能存在性能问题"
fi
echo ""

# 生产者速率配置验证
echo "=== 生产者速率配置 ==="
echo "Video配置:"
grep "VideoFileSource detected" test_fifo_30s_final.log || echo "  未找到VideoFileSource日志"
echo ""

echo "Audio配置:"
grep "AudioFileSource detected" test_fifo_30s_final.log || echo "  未找到AudioFileSource日志"
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
echo "详细日志: test_fifo_30s_final.log"
echo ""
