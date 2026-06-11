#!/bin/bash
# analyze_profile.sh - host 端内存 profile 分析
# T32 只负责记录原始数据(T0.log...T8.log),host 端做汇总分析
#
# 用法(host 上,完整工具链):
#   cd /home/zengping/project/huntcam/code/t32_cam
#   tools/analyze_profile.sh                          # 最新 profile
#   tools/analyze_profile.sh build/logs/mem-profile-XXX  指定 profile
#   tools/analyze_profile.sh --compare                对比所有 profile

set -euo pipefail

PROFILE_DIR=""
COMPARE_MODE=false
for arg in "$@"; do
    case "$arg" in
        --compare|-c) COMPARE_MODE=true ;;
        --help|-h) sed -n '2,15p' "$0"; exit 0 ;;
        *) PROFILE_DIR="$arg" ;;
    esac
done

# 找最新 profile 目录
if [ -z "$PROFILE_DIR" ]; then
    PROFILE_DIR=$(ls -d build/logs/mem-profile-* 2>/dev/null | sort -r | head -1 || echo "")
fi
if [ -z "$PROFILE_DIR" ] || [ ! -d "$PROFILE_DIR" ]; then
    echo "ERROR: 没找到 profile 目录"
    echo "  试用:tools/analyze_profile.sh build/logs/mem-profile-XXX"
    exit 1
fi

# ========== 解析函数 ==========
# 从 /proc/meminfo 抓取字段(file 内格式:MemFree:           33292 kB)
parse_field() {
    local f=$1
    local field=$2
    [ ! -f "$f" ] && { echo "N/A"; return; }
    grep "^${field}:" "$f" 2>/dev/null | head -1 | awk '{print $2}' | tr -d 'kB'
}

# 抓 zram mm_stat 的某一列(file 格式:"--- zram mm_stat ---" 下一行是数据)
parse_zram_col() {
    local f=$1
    local col=$2
    [ ! -f "$f" ] && { echo "N/A"; return; }
    # "--- zram mm_stat ---" 下面一行是数据
    local data_line=$(awk '/^--- zram mm_stat ---/{getline; print; exit}' "$f" 2>/dev/null)
    [ -z "$data_line" ] && { echo "N/A"; return; }
    local val=$(echo "$data_line" | awk -v c="$col" '{print $c}')
    [ -n "$val" ] && echo "$val" || echo "N/A"
}

# 进程是否还活:看 /proc/<pid>/status 是否在 dump 里
parse_process_alive() {
    local f=$1
    [ ! -f "$f" ] && { echo "N/A"; return; }
    if grep -qE "^VmRSS:" "$f" 2>/dev/null; then
        echo "yes"
    else
        echo "no"
    fi
}

parse_process_rss() {
    local f=$1
    [ ! -f "$f" ] && { echo "N/A"; return; }
    grep "^VmRSS:" "$f" 2>/dev/null | head -1 | awk '{print $2}'
}

# ========== 生成单个 profile summary ==========
generate_summary() {
    local pdir=$1
    PROFILE_DIR="$pdir"
    local label=$(basename "$pdir")

    echo "=================================================================="
    echo "  Profile: $label"
    echo "  Path:    $pdir"
    if [ -f "$pdir/rc.log" ]; then
        echo "  Result:  $(cat "$pdir/rc.log")"
    fi
    echo "=================================================================="
    printf "%-7s | %-7s | %-7s | %-7s | %-7s | %-5s | %-7s | %-7s | %-7s | %-9s | %-9s | %-5s | %-4s | %-7s\n" \
        "ts" "MemFree" "Buffers" "Cached" "AnonPgs" "Dirty" "SwapFree" "CmaFree" "VmRSS" "zram_data" "zram_fail" "ComAS" "alive" "备注"
    echo "---------+---------+---------+---------+---------+-------+---------+---------+---------+-----------+-----------+-------+------+-------"

    for ts in T0 T1 T1.5 T2 T2.5 T3 T3.5 T4 T4.5 T5 T5.5 T6 T6.5 T7 T8; do
        local f="$pdir/$ts.log"
        [ ! -f "$f" ] && continue

        local memfree=$(parse_field "$f" MemFree)
        local buffers=$(parse_field "$f" Buffers)
        local cached=$(parse_field "$f" Cached)
        local anon=$(parse_field "$f" AnonPages)
        local dirty=$(parse_field "$f" Dirty)
        local swapfree=$(parse_field "$f" SwapFree)
        local cmafree=$(parse_field "$f" CmaFree)
        local comm_as_kb=$(parse_field "$f" Committed_AS)
        local comm_as=$((comm_as_kb / 1024))  # MB
        local vmrss=$(parse_process_rss "$f")
        local zram_data=$(parse_zram_col "$f" 2)   # compr_data_size (bytes)
        local zram_fail=$(parse_zram_col "$f" 7)   # fail_pages
        local alive=$(parse_process_alive "$f")

        # 备注
        local note=""
        case "$ts" in
            T0) note="baseline" ;;
            T1) note="SDK init" ;;
            T1.5) note="rec 1s" ;;
            T2) note="rec 5s" ;;
            T2.5) note="rec 10s" ;;
            T3) note="rec 15s" ;;
            T3.5) note="rec 20s" ;;
            T4) note="rec 25s" ;;
            T4.5) note="rec 28s" ;;
            T5) note="record done" ;;
            T5.5) note="+0.5s" ;;
            T6) note="release" ;;
            T6.5) note="+0.5s" ;;
            T7) note="desc write" ;;
            T8) note="+30s settle" ;;
        esac

        # zram_data 转 KB
        local zram_data_kb=""
        if [ -n "$zram_data" ] && [ "$zram_data" != "N/A" ]; then
            zram_data_kb=$((zram_data / 1024))KB
        else
            zram_data_kb="N/A"
        fi

        printf "%-7s | %-7s | %-7s | %-7s | %-7s | %-5s | %-7s | %-7s | %-7s | %-9s | %-9s | %-5s | %-4s | %-7s\n" \
            "$ts" "$memfree" "$buffers" "$cached" "$anon" "$dirty" "$swapfree" "$cmafree" "$vmrss" \
            "$zram_data_kb" "$zram_fail" "${comm_as}M" "$alive" "$note"
    done

    echo ""
    echo "  zram mm_stat 字段含义(7 列):"
    echo "    1: orig_data_size    2: compr_data_size    3: mem_used_total"
    echo "    4: mem_limit         5: mem_used_max      6: pages_stored"
    echo "    7: fail_pages        ★ zram 风暴指标(分配失败次数)"
    echo ""
}

# ========== 对比模式 ==========
compare_profiles() {
    echo "=================================================================="
    echo "  对比模式:所有 profile 在 T4(录制 25s 内存压力峰值附近)"
    echo "=================================================================="
    local profiles=($(ls -d build/logs/mem-profile-* 2>/dev/null | sort))

    if [ ${#profiles[@]} -eq 0 ]; then
        echo "  没找到 profile"
        return
    fi

    # Header
    printf "%-13s |" "field"
    for p in "${profiles[@]}"; do
        local label=$(basename "$p" | sed 's/mem-profile-[0-9-]*-//')
        printf " %-25s" "$label"
    done
    echo ""
    echo "---------------+-------------------+-------------------+-------------------+"

    # 关键字段
    for field in MemFree Cached AnonPages Dirty CmaFree SwapFree Committed_AS VmRSS; do
        printf "%-13s |" "$field"
        for p in "${profiles[@]}"; do
            local f="$p/T4.log"
            local val=$(parse_field "$f" "$field")
            printf " %-25s" "${val:-N/A}"
        done
        echo ""
    done
    echo ""

    # zram 关键指标
    echo "T4 时刻 zram mm_stat:"
    for col in 2 3 7 8; do
        local col_name=""
        case $col in
            2) col_name="compr_data_size" ;;
            3) col_name="mem_used_total" ;;
            7) col_name="pages_stored" ;;
            8) col_name="fail_pages" ;;
        esac
        printf "  %-17s |" "$col_name"
        for p in "${profiles[@]}"; do
            local val=$(parse_zram_col "$p/T4.log" "$col")
            printf " %-25s" "${val:-N/A}"
        done
        echo ""
    done
    echo ""

    echo "T5 时刻 zram mm_stat(录影刚结束):"
    for col in 2 3 7 8; do
        printf "  %-17s |" "$col $col_name"
        for p in "${profiles[@]}"; do
            local val=$(parse_zram_col "$p/T5.log" "$col")
            printf " %-25s" "${val:-N/A}"
        done
        echo ""
    done
    echo ""

    # 进程状态
    echo "T5 时刻 htc_main_app 状态:"
    printf "  %-13s |" "alive"
    for p in "${profiles[@]}"; do
        local alive=$(parse_process_alive "$p/T5.log")
        printf " %-25s" "$alive"
    done
    echo ""
    echo ""
}

# ========== 列出 dmesg 关键事件 ==========
show_zram_events() {
    local pdir=$1
    if [ ! -f "$pdir/dmesg.log" ]; then
        return
    fi
    local count=$(grep -cE "zram|Out of memory|Killed process" "$pdir/dmesg.log" 2>/dev/null || echo 0)
    echo "  dmesg 关键事件: $count 行 (zram/OOM/Killed)"
    if [ "$count" -gt 0 ] && [ "$count" -lt 30 ]; then
        echo "  ---"
        grep -E "zram|Out of memory|Killed process" "$pdir/dmesg.log" 2>/dev/null
    elif [ "$count" -ge 30 ]; then
        echo "  --- (前 10 行)"
        grep -E "zram|Out of memory|Killed process" "$pdir/dmesg.log" 2>/dev/null | head -10
    fi
}

# ========== 主流程 ==========
cd "$(dirname "$0")/.." || exit 1

if [ "$COMPARE_MODE" = true ]; then
    compare_profiles
    for p in $(ls -d build/logs/mem-profile-* 2>/dev/null | sort); do
        echo "  $(basename "$p"):"
        show_zram_events "$p"
    done
else
    generate_summary "$PROFILE_DIR"
    show_zram_events "$PROFILE_DIR"
fi

echo "=================================================================="
