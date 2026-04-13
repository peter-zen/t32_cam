#!/bin/bash

# 安装 ffplay 作为 RTSP 客户端用于调试

echo "========================================"
echo "安装 FFmpeg（包含 ffplay）"
echo "========================================"
echo ""
echo "正在更新包列表..."
sudo apt-get update

echo ""
echo "正在安装 FFmpeg..."
sudo apt-get install -y ffmpeg

echo ""
echo "========================================"
echo "验证安装"
echo "========================================"

if command -v ffplay &> /dev/null; then
    echo "✅ ffplay 安装成功！"
    echo ""
    echo "版本信息："
    ffplay -version | head -3
    echo ""
    echo "========================================"
    echo "使用方法："
    echo "========================================"
    echo ""
    echo "1. 播放 RTSP 流："
    echo "   ffplay rtsp://localhost:8554/live"
    echo ""
    echo "2. 播放 10 秒后退出："
    echo "   ffplay -t 10 rtsp://localhost:8554/live"
    echo ""
    echo "3. 无日志播放："
    echo "   ffplay -loglevel quiet rtsp://localhost:8554/live"
    echo ""
    echo "4. 调试模式："
    echo "   ffplay -loglevel debug rtsp://localhost:8554/live"
    echo ""
    echo "5. 查看帮助："
    echo "   ffplay -h"
    echo ""
else
    echo "❌ ffplay 安装失败，请检查错误信息"
    exit 1
fi
