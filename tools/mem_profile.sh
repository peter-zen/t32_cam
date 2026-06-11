#!/bin/sh
# T32 内存 profiling —— 一次跑一个 env 条件
# 只负责记录原始数据,不做分析(T32 busybox 工具受限)
#
# 用法(T32 上):
#   cd /mnt/huntcam
#   sh mem_profile.sh baseline
#   sh mem_profile.sh "HTC_RECORD_NO_DESC=1"
#   sh mem_profile.sh "HTC_RECORD_NO_JSON_BUILD=1"
#   sh mem_profile.sh "HTC_NO_RELEASE=1"
#
# 输出目录:/mnt/huntcam/logs/mem-profile-<date>-<label>/
#   T0.log ... T8.log    原始 /proc/meminfo + /proc/vmstat + zram mm_stat + 进程 smaps
#   htc.log             htc_main_app 完整输出
#   dmesg.log           本次跑的 dmesg 切片(可能含 zram 错误)
#
# 配套 host 端分析脚本:tools/analyze_profile.sh

LABEL=${1:-baseline}
HTC_EXTRA_ENV=${2:-}
LOG_DIR=/mnt/huntcam/logs
PROFILE_DIR="$LOG_DIR/mem-profile-$(date '+%Y%m%d-%H%M%S')-$LABEL"
mkdir -p "$PROFILE_DIR"
HTC_BIN=/mnt/huntcam/bin/htc_main_app
PID_LOG="$PROFILE_DIR/h.pid"
WATCHDOG_SEC=180

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
        echo "--- /proc/zoneinfo (key fields) ---"
        grep -E '^(Node|min|low|high|present|managed|spanned|protected|free|high:free|high:managed)"' /proc/zoneinfo 2>/dev/null | head -20
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
                echo "--- /proc/$pid/stack (only first 3 tasks) ---"
                cat "/proc/$pid/stack" 2>/dev/null | head -3
            else
                echo "--- htc_main_app PID=$pid 已死 ---"
            fi
        fi
    } > "$outfile" 2>&1
}

# 启动 htc_main_app
if [ -n "$HTC_EXTRA_ENV" ]; then
    env $HTC_EXTRA_ENV "$HTC_BIN" -wm 0 -rtc 1 > "$PROFILE_DIR/htc.log" 2>&1 &
else
    "$HTC_BIN" -wm 0 -rtc 1 > "$PROFILE_DIR/htc.log" 2>&1 &
fi
HTC_PID=$!
echo $HTC_PID > "$PID_LOG"
echo ">>> $LABEL (env=$HTC_EXTRA_ENV), PID=$HTC_PID"

# watchdog
(
    sleep $WATCHDOG_SEC
    if kill -0 $HTC_PID 2>/dev/null; then
        echo "!!! watchdog: htc_main_app 还在跑,$WATCHDOG_SEC s 内没退出,强杀" > "$PROFILE_DIR/watchdog.log"
        kill -9 $HTC_PID 2>/dev/null
    fi
) &
WATCHDOG_PID=$!

# 14 个时间点 dump
sleep 0.2; dump_snapshot T0
sleep 2; dump_snapshot T1
sleep 1; dump_snapshot T1.5
sleep 4; dump_snapshot T2
sleep 5; dump_snapshot T2.5
sleep 5; dump_snapshot T3
sleep 5; dump_snapshot T3.5
sleep 5; dump_snapshot T4
sleep 3; dump_snapshot T4.5

wait $HTC_PID
HTC_RC=$?
echo "htc_main_app rc=$HTC_RC" > "$PROFILE_DIR/rc.log"
kill $WATCHDOG_PID 2>/dev/null
wait $WATCHDOG_PID 2>/dev/null

dump_snapshot T5
sleep 0.5; dump_snapshot T5.5
sleep 1; dump_snapshot T6
sleep 0.5; dump_snapshot T6.5
sleep 1; dump_snapshot T7
sleep 28; dump_snapshot T8

# 抓 dmesg
dmesg > "$PROFILE_DIR/dmesg.log" 2>&1

echo ">>> done,输出在 $PROFILE_DIR"
