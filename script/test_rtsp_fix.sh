#!/bin/bash

# RTSP 服务器修复验证脚本
echo "=== RTSP Server Fix Verification ==="

# 清理旧的日志
echo "Cleaning old logs..."
mkdir -p sim_sdcard/log
rm -f sim_sdcard/log/app.log

# 启动RTSP服务器测试（后台运行）
echo "Starting RTSP server..."
./build_sim/bin/htc_main_app -rs > rtsp_test.log 2>&1 &
RTSP_PID=$!

# 等待服务器启动
echo "Waiting for server to start..."
sleep 3

# 检查服务器是否正在运行
if kill -0 $RTSP_PID 2>/dev/null; then
    echo "✓ RTSP server started successfully (PID: $RTSP_PID)"
else
    echo "✗ RTSP server failed to start"
    exit 1
fi

# 等待一段时间让服务器产生日志
echo "Generating test logs for 10 seconds..."
sleep 10

# 停止服务器
echo "Stopping RTSP server..."
kill -INT $RTSP_PID

# 等待服务器停止
sleep 2

# 检查服务器是否成功停止
if kill -0 $RTSP_PID 2>/dev/null; then
    kill -KILL $RTSP_PID
    echo "✓ Server was forcefully killed"
else
    echo "✓ Server stopped gracefully"
fi

# 分析日志
echo ""
echo "=== Log Analysis ==="

if [ -f "sim_sdcard/log/app.log" ]; then
    echo "✓ Log file created"
    
    # 检查修复相关的日志信息
    echo ""
    echo "Checking improvements:"
    
    # 检查FIFO水位监控
    if grep -q "FIFO.*high watermark" sim_sdcard/log/app.log; then
        echo "✓ FIFO high watermark monitoring active"
        grep "FIFO.*high watermark" sim_sdcard/log/app.log | wc -l | xargs echo "  - Found watermark warnings:"
    else
        echo "? FIFO high watermark monitoring not detected"
    fi
    
    # 检查音频循环重试
    if grep -q "looped.*retrying" sim_sdcard/log/app.log; then
        echo "✓ Audio file loop retry mechanism active"
    else
        echo "? Audio file loop retry not detected (may not have looped yet)"
    fi
    
    # 检查丢帧处理
    if grep -q "Dropped.*frame.*overflow" sim_sdcard/log/app.log; then
        echo "✓ Frame drop mechanism active"
    else
        echo "? Frame drop not occurred (good performance)"
    fi
    
    # 统计错误
    fifo_empty=$(grep -c "FIFO.*empty" sim_sdcard/log/app.log || echo "0")
    audio_empty=$(grep -c "FIFO-A.*empty" sim_sdcard/log/app.log || echo "0")
    
    echo ""
    echo "Empty FIFO occurrences:"
    echo "  - Video: $fifo_empty"
    echo "  - Audio: $audio_empty"
    
    # 检查是否有严重的错误
    critical_errors=$(grep -c -E "(Audio stream ended|Video stream ended)" sim_sdcard/log/app.log || echo "0")
    if [ $critical_errors -eq 0 ]; then
        echo "✓ No stream termination errors detected"
    else
        echo "⚠ Stream termination errors: $critical_errors"
    fi
    
else
    echo "✗ Log file not found"
fi

echo ""
echo "=== Test Summary ==="
echo "The following fixes have been implemented:"
echo "1. ✓ Increased FIFO capacity (Video: 10→20, Audio: 20→40)"
echo "2. ✓ Improved audio loop retry logic"
echo "3. ✓ Added FIFO watermark monitoring"
echo "4. ✓ Reduced wait times for better responsiveness"
echo "5. ✓ Enhanced error differentiation (-1 retry vs -2 end)"
echo "6. ✓ Added frame drop mechanism to prevent blocking"

echo ""
echo "To test with a client:"
echo "ffplay rtsp://localhost:554/live"
