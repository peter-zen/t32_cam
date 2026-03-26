#!/bin/bash

# 快速验证脚本 - 检查 RTSP 服务器的音频和视频流
# 使用 ffprobe 快速分析流信息

echo "========================================="
echo "  Quick RTSP Stream Verification"
echo "========================================="
echo ""

# 检查服务器是否在运行
if ! pgrep -x htc_main_app > /dev/null; then
    echo "RTSP server is not running. Starting..."
    ./build_sim/bin/htc_main_app -rs > /dev/null 2>&1 &
    RTSP_PID=$!
    echo "Waiting for server to start..."
    sleep 3
    echo ""
fi

echo "Analyzing RTSP stream at rtsp://localhost:554/live"
echo ""

# 使用 ffprobe 分析流
if command -v ffprobe &> /dev/null; then
    echo "--- Stream Information ---"
    ffprobe -v quiet -show_streams -show_format rtsp://localhost:554/live 2>&1 | \
        grep -E "(codec_name|codec_type|width|height|sample_rate|channels|r_frame_rate|bit_rate)" | \
        while IFS= read -r line; do
            if [[ $line == *"codec_type=video"* ]]; then
                echo ""
                echo "📹 VIDEO STREAM:"
            elif [[ $line == *"codec_type=audio"* ]]; then
                echo ""
                echo "🎵 AUDIO STREAM:"
            fi
            echo "  $line"
        done

    echo ""
    echo "--- Quick Summary ---"

    # 提取关键信息
    VIDEO=$(ffprobe -v quiet -show_streams rtsp://localhost:554/live 2>&1 | grep -c "codec_type=video" || echo "0")
    AUDIO=$(ffprobe -v quiet -show_streams rtsp://localhost:554/live 2>&1 | grep -c "codec_type=audio" || echo "0")

    if [ "$VIDEO" -gt 0 ]; then
        WIDTH=$(ffprobe -v quiet -show_streams rtsp://localhost:554/live 2>&1 | grep -A 10 "codec_type=video" | grep "width=" | cut -d= -f2)
        HEIGHT=$(ffprobe -v quiet -show_streams rtsp://localhost:554/live 2>&1 | grep -A 10 "codec_type=video" | grep "height=" | cut -d= -f2)
        echo "✓ Video: ${WIDTH}x${HEIGHT}"
    else
        echo "✗ Video: NOT DETECTED"
    fi

    if [ "$AUDIO" -gt 0 ]; then
        RATE=$(ffprobe -v quiet -show_streams rtsp://localhost:554/live 2>&1 | grep -A 10 "codec_type=audio" | grep "sample_rate=" | cut -d= -f2)
        CHAN=$(ffprobe -v quiet -show_streams rtsp://localhost:554/live 2>&1 | grep -A 10 "codec_type=audio" | grep "channels=" | cut -d= -f2)
        echo "✓ Audio: ${RATE} Hz, ${CHAN} channels"
    else
        echo "✗ Audio: NOT DETECTED"
    fi

    echo ""

    if [ "$VIDEO" -gt 0 ] && [ "$AUDIO" -gt 0 ]; then
        echo "✅ RESULT: BOTH AUDIO AND VIDEO STREAMS ARE WORKING"
        exit 0
    elif [ "$VIDEO" -gt 0 ]; then
        echo "⚠️  RESULT: ONLY VIDEO STREAM DETECTED"
        exit 1
    elif [ "$AUDIO" -gt 0 ]; then
        echo "⚠️  RESULT: ONLY AUDIO STREAM DETECTED"
        exit 1
    else
        echo "❌ RESULT: NO STREAMS DETECTED"
        exit 1
    fi

else
    echo "❌ ffprobe not found. Please install FFmpeg first:"
    echo "   sudo apt-get install ffmpeg"
    exit 1
fi
