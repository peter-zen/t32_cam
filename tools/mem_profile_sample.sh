#!/bin/sh
# T32 内存 profiling —— sample-Encoder-video 对照组
#
# 跟 htc_main_app profile 的差异:
#   - 跑 sample-Encoder-video (裸 SDK, 无 thumbnail/JSON/DB/MCU/HTTP)
#   - 同样的 7 个时间点 dump meminfo/vmstat/buddyinfo/zram mm_stat
#   - 同样的 dmesg 切片对比 zram 错误数
#
# 目的: 验证"SDK 自身在 6 Mbps/2K 30s 下是否也撞 zram 风暴"
#       如果 sample 在更低 bitrate (1 Mbps) 都不撞 → 我们 app 加重了负担
#       如果 sample 也撞 → 是 64 MB 设备 + 录影 30s 的固有问题
#
# 用法(T32 上):
#   cd /mnt/huntcam
#   sh mem_profile_sample.sh
#
# 配套 host 端分析脚本:tools/analyze_profile.sh (直接复用)

LABEL=${1:-sample-baseline}
LOG_DIR=/mnt/huntcam/logs
PROFILE_DIR="$LOG_DIR/mem-profile-$(date '+%Y%m%d-%H%M%S')-$LABEL"
mkdir -p "$PROFILE_DIR"
SAMPLE_BIN=/mnt/huntcam/bin/sample-Encoder-video
PID_LOG="$PROFILE_DIR/h.pid"

echo ">>> 输出目录:$PROFILE_DIR"

dump_snapshot() {
    local ts=$1
    local outfile="$PROFILE_DIR/$ts.log"
    {
        echo "=== $ts $(date '+%H:%M:%S.%N') ==="
        echo "--- /proc/meminfo (key) ---"
        grep -E '^(MemTotal|MemFree|MemAvailable|Buffers|Cached|AnonPages|Active\(anon\)|Inactive\(anon\)|SwapTotal|SwapFree|Dirty|Writeback|Slab|SReclaimable|CmaTotal|CmaFree|CommitLimit|Committed_AS):' /proc/meminfo 2>/dev/null
        echo ""
        echo "--- /proc/vmstat (key) ---"
        grep -E '^(pgfault|pgmajfault|pgscan|pgsteal|pswpin|pswpout|allocstall|nr_free_pages|nr_dirty|nr_writeback|nr_anon_pages|nr_mapped)' /proc/vmstat 2>/dev/null
        echo ""
        echo "--- /proc/buddyinfo ---"
        cat /proc/buddyinfo 2>/dev/null
        echo ""
        echo "--- zram mm_stat ---"
        cat /sys/block/zram0/mm_stat 2>/dev/null || echo "(no zram)"
        echo ""
        if [ -f "$PID_LOG" ]; then
            local pid=$(cat "$PID_LOG")
            if [ -d "/proc/$pid" ]; then
                echo "--- /proc/$pid/status ---"
                cat "/proc/$pid/status" 2>/dev/null
                echo ""
                echo "--- /proc/$pid/smaps_rollup ---"
                cat "/proc/$pid/smaps_rollup" 2>/dev/null
                echo ""
            else
                echo "--- sample-Encoder-video PID=$pid 已死 ---"
            fi
        fi
    } > "$outfile" 2>&1
}

# 启动 sample-Encoder-video
"$SAMPLE_BIN" > "$PROFILE_DIR/sample.log" 2>&1 &
SAMPLE_PID=$!
echo $SAMPLE_PID > "$PID_LOG"
echo ">>> $LABEL, PID=$SAMPLE_PID"

# 14 个时间点 dump,时间点针对 sample 的 stage 调过
# 假设: 5s sleep + 30s recording + ~5s teardown
sleep 0.3;  dump_snapshot T0    # 启动瞬间 (system_init 进行中)
sleep 1.5;  dump_snapshot T1    # post framesource + encoder create
sleep 2.5;  dump_snapshot T1.5  # post bind + streamon
sleep 2.5;  dump_snapshot T2    # 5s sleep 期间,ISP warm-up
sleep 3.0;  dump_snapshot T2.5  # sleep 快结束
sleep 5.0;  dump_snapshot T3    # 录影开始 0-2s
sleep 5.0;  dump_snapshot T3.5  # 录影 5-7s
sleep 5.0;  dump_snapshot T4    # 录影 10-12s (稳态)
sleep 5.0;  dump_snapshot T4.5  # 录影 15-17s
sleep 5.0;  dump_snapshot T5    # 录影 20-22s
sleep 5.0;  dump_snapshot T5.5  # 录影 25-27s (近结束)
sleep 3.0;  dump_snapshot T6    # 录影 28-30s (临界)

# 等待 sample 自然结束(get stream 完成后会 streamoff + destroy + exit)
wait $SAMPLE_PID
SAMPLE_RC=$?
echo "sample-Encoder-video rc=$SAMPLE_RC" > "$PROFILE_DIR/rc.log"

# 关键: 录完后的 teardown 阶段也打点 (对应 htc_main_app 的 releaseVideoResources)
# 如果 sample 死太快, 这些 snapshot 会显示 "PID 已死"
sleep 0.2;  dump_snapshot T6.5  # 录完 ~0.2s (streamoff 进行中)
sleep 0.5;  dump_snapshot T7    # 录完 ~0.7s (unbind + video_exit)
sleep 0.5;  dump_snapshot T7.5  # 录完 ~1.2s (encoder destroy)
sleep 0.5;  dump_snapshot T8    # 录完 ~1.7s (framesource_exit + system_exit)

# 抓 dmesg
dmesg > "$PROFILE_DIR/dmesg.log" 2>&1

echo ">>> done,输出在 $PROFILE_DIR"
echo ">>> 对照命令:tools/analyze_profile.sh $PROFILE_DIR"
