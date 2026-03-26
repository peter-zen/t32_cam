#!/bin/bash

# RTSP 音视频同步测试脚本

echo "========================================"
echo "RTSP 音视频同步测试"
echo "========================================"

# 清理旧进程
pkill -f htc_main_app
pkill -f ffplay
sleep 2

# 启动 RTSP 服务器
echo "1. 启动 RTSP 服务器..."
./build_sim/bin/htc_main_app -rs > /tmp/rtsp_server_sync.log 2>&1 &
SERVER_PID=$!

echo "   RTSP 服务器 PID: $SERVER_PID"
echo "   等待服务器启动..."
sleep 3

# 检查服务器是否正常启动
if ! ps -p $SERVER_PID > /dev/null 2>&1; then
    echo "❌ RTSP 服务器启动失败"
    cat /tmp/rtsp_server_sync.log | tail -30
    exit 1
fi

echo "   ✅ RTSP 服务器启动成功"
echo ""
echo "2. 检查延迟启动功能..."
tail -50 /tmp/rtsp_server_sync.log | grep -E "waiting for client connection"

if [ $? -eq 0 ]; then
    echo "   ✅ 延迟启动功能正常（未立即打开文件）"
else
    echo "   ⚠️  延迟启动可能有问题"
fi
echo ""

# 启动 ffplay
echo "3. 使用 ffplay 连接 RTSP 流..."
ffplay -loglevel info -t 15 -nodisp rtsp://localhost:554/live 2>&1 | tee /tmp/ffplay_sync.log &
FFPLAY_PID=$!

echo "   ffplay PID: $FFPLAY_PID"
echo "   等待客户端连接..."
sleep 5
echo ""
echo "4. 检查客户端连接事件..."
tail -50 /tmp/rtsp_server_sync.log | grep -E "Client connected|VideoFileSource::open|MediaSession started"

if [ $? -eq 0 ]; then
    echo "   ✅ 客户端连接成功，延迟启动功能工作正常"
else
    echo "   ⚠️  客户端连接可能有问题"
fi
echo ""

# 等待播放一段时间
echo "5. 播放 15 秒，测试音视频同步..."
sleep 10

# 检查同步问题
echo ""
echo "6. 检查音视频同步..."
echo "   检查 ffplay 日志中的同步警告："
grep -i "sync\|frame\|audio\|video" /tmp/ffplay_sync.log | head -20
echo ""

# 检查 RTSP 服务器日志中的帧统计
echo "7. 检查 RTSP 服务器统计..."
tail -100 /tmp/rtsp_server_sync.log | grep -E "Frames produced|dropped|FIFO"
echo ""

# 停止测试
echo ""
echo "8. 停止测试..."
kill $FFPLAY_PID 2>/dev/null
kill $SERVER_PID 2>/dev/null
sleep 2

# 检查客户端断开事件
echo "9. 检查客户端断开事件..."
tail -50 /tmp/rtsp_server_sync.log | grep -E "Client disconnected|stopped|cleaned up"

if [ $? -eq 0 ]; then
    echo "   ✅ 客户端断开正常，资源清理功能工作正常"
else
    echo "   ⚠️  客户端断开可能有问题"
fi
echo ""

echo "========================================"
echo "测试完成"
echo "========================================"
echo ""
echo "日志文件："
echo "  RTSP 服务器: /tmp/rtsp_server_sync.log"
echo "  ffplay:       /tmp/ffplay_sync.log"
echo ""
echo "查看完整日志："
echo "  cat /tmp/rtsp_server_sync.log"
echo "  cat /tmp/ffplay_sync.log"
