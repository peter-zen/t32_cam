#!/bin/sh
# mem_profile_um.sh v2 — um preview OOM burst 定位
#
# 相比 v1：2s 粒度采样 + 当前 VmSize + [heap] 大小 + **VmSize 暴涨瞬间 dump
# /proc/<pid>/maps**（定位是 [heap] / anon mmap / IMP 设备映射哪个在涨）。
#
# 用法（T32 上）:
#   cd /mnt/huntcam
#   sh tools/mem_profile_um.sh            # 干净跑（推荐第一轮）
#   sh tools/mem_profile_um.sh debug      # +HTC_LOG_DEBUG=1（frame-level，第二轮）
#
# 交回（host 侧 build/logs/<输出目录>/ 同名可直接读）:
#   trend.csv          每 2s 一行：VmSize/VmRSS/VmPeak/heap/Committed/Anon/Slab/MemFree/zram
#   maps-T001.log      启动 baseline 的完整 maps（对照用）
#   maps-burst-Tnnn.log VmSize 暴涨瞬间的 maps ← 决定性证据（哪个 region ballooned）
#   um.log / dmesg.log / app.log

LABEL=${1:-baseline}
DEBUG=0
[ "$LABEL" = "debug" ] && DEBUG=1

BIN=/mnt/huntcam/bin/um
LOG_DIR=/mnt/huntcam/logs
PROFILE_DIR="$LOG_DIR/mem-profile-um-$(date '+%Y%m%d-%H%M%S')-$LABEL"
mkdir -p "$PROFILE_DIR"
PID_LOG="$PROFILE_DIR/um.pid"
TREND="$PROFILE_DIR/trend.csv"
MAX_WAIT_SEC=600
SAMPLE_SEC=2
BURST_THRESHOLD_KB=20480    # VmSize 单次跳 >20MB 触发 maps dump
prev_vmsize=0
burst_dumped=0

echo ">>> 输出目录: $PROFILE_DIR"

# 起 um
if [ "$DEBUG" = "1" ]; then
    HTC_LOG_DEBUG=1 HTC_UM_IDLE_TIMEOUT_MS=600000 "$BIN" > "$PROFILE_DIR/um.log" 2>&1 &
else
    HTC_UM_IDLE_TIMEOUT_MS=600000 "$BIN" > "$PROFILE_DIR/um.log" 2>&1 &
fi
UM_PID=$!
echo $UM_PID > "$PID_LOG"
echo ">>> um PID=$UM_PID debug=$DEBUG — 连 APP 进预览，等 burst/OOM。每 ${SAMPLE_SEC}s 采样。"

# NOTE: T32 busybox 无 awk — 用 set -- 词分割 / read 取首字段
g_status() { set -- $(grep -m1 "^$1:" "/proc/$UM_PID/status" 2>/dev/null); echo "$2"; }
g_mem()    { set -- $(grep -m1 "^$1:" /proc/meminfo 2>/dev/null); echo "$2"; }
heap_kb() {
    # /proc/<pid>/maps 里 [heap] 行的地址区间，shell 算术转 hex
    line=$(grep -m1 '\[heap\]' "/proc/$UM_PID/maps" 2>/dev/null)
    [ -z "$line" ] && { echo 0; return; }
    range=${line%% *}; start=${range%%-*}; end=${range#*-}
    [ -z "$start" ] || [ -z "$end" ] && { echo 0; return; }
    echo $(( (0x$end - 0x$start) / 1024 ))
}
zram_orig() { read z _ < /sys/block/zram0/mm_stat 2>/dev/null; echo "$z"; }

echo "snap,time,VmSize_kB,VmRSS_kB,VmPeak_kB,heap_kB,Committed_AS_kB,AnonPages_kB,Slab_kB,MemFree_kB,zram_orig_B" > "$TREND"

i=0
while kill -0 $UM_PID 2>/dev/null; do
    i=$((i + 1))
    snap="T$(printf '%03d' $i)"
    vmsize=$(g_status VmSize); vmrss=$(g_status VmRSS); vmpeak=$(g_status VmPeak)
    heap=$(heap_kb)
    comm=$(g_mem Committed_AS); anon=$(g_mem AnonPages); slab=$(g_mem Slab); memfree=$(g_mem MemFree)
    zr=$(zram_orig)
    echo "$snap,$(date '+%H:%M:%S'),$vmsize,$vmrss,$vmpeak,$heap,$comm,$anon,$slab,$memfree,$zr" >> "$TREND"

    # baseline maps（首帧，对照）
    if [ "$i" = 1 ]; then
        cp "/proc/$UM_PID/maps" "$PROFILE_DIR/maps-T001.log" 2>/dev/null
    fi

    # burst 检测：VmSize 单次跳 > 阈值 → dump maps 一次
    if [ -n "$vmsize" ] && [ "$burst_dumped" = "0" ] && [ "$prev_vmsize" -gt 0 ] 2>/dev/null; then
        delta=$((vmsize - prev_vmsize))
        if [ "$delta" -gt "$BURST_THRESHOLD_KB" ] 2>/dev/null; then
            echo ">>> BURST $snap: VmSize ${prev_vmsize}->${vmsize} (+${delta} kB) — dumping maps"
            cp "/proc/$UM_PID/maps" "$PROFILE_DIR/maps-burst-$snap.log" 2>/dev/null
            cat "/proc/$UM_PID/smaps_rollup" > "$PROFILE_DIR/smaps-burst-$snap.log" 2>/dev/null
            burst_dumped=1
        fi
    fi
    prev_vmsize=$vmsize

    sleep $SAMPLE_SEC
    if [ $((i * SAMPLE_SEC)) -ge $MAX_WAIT_SEC ]; then
        echo ">>> 上限 ${MAX_WAIT_SEC}s，um 还活着——未复现 OOM"
        break
    fi
done

wait $UM_PID 2>/dev/null
echo "um rc=$?" > "$PROFILE_DIR/rc.log"
dmesg > "$PROFILE_DIR/dmesg.log" 2>&1
cp "$LOG_DIR/app.log" "$PROFILE_DIR/app.log" 2>/dev/null

echo ""
echo ">>> done。交回（NFS host 侧 build/logs/<同名目录>/ 可直读）:"
echo "    $PROFILE_DIR/trend.csv             ← 2s 粒度内存趋势"
echo "    $PROFILE_DIR/maps-burst-Tnnn.log   ← 暴涨瞬间的 maps（决定性）"
echo "    $PROFILE_DIR/um.log / dmesg.log"
