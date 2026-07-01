// test_elog_async.cpp — verifies the ELOG_ASYNC_OUTPUT_ENABLE path.
//
// Backs the async-elog change (elog_cfg.h + elog_async.c + elog_port.c consumer
// thread + Misc::poweroff drain). Locks four properties:
//   (a) LATENCY   — per-call elog_i stays bounded (< 5 ms) under an 8-thread
//                   burst with the FILE sink active. Sync mode serializes every
//                   caller on per-line fwrite+fflush (multi-ms..10s of ms), so a
//                   sub-5 ms bound under this load is direct evidence the hot
//                   thread is no longer blocked on I/O.
//   (b) DROP/CNT  — a single-thread flood (20k lines) overflows the 128-slot
//                   ring → drop-oldest bumps elog_async_get_drop_count() > 0.
//   (c) ORDER     — single-thread sequence survives in strictly increasing
//                   order (drop-oldest preserves order among survivors).
//   (d) DRAIN     — the final log line survives elog_deinit_all() (the consumer
//                   drains the ring + final-flushes before join).
//
// Hard invariant across (a)+(b)+(c): written + dropped == logged.

#include <elog.h>
#include <ElogInit.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// Extension exported by elog_async.c (not in upstream elog.h).
extern "C" size_t elog_async_get_drop_count(void);

static long long us_between(std::chrono::steady_clock::time_point a,
                            std::chrono::steady_clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

static long long count_substr(const std::string& hay, const std::string& needle) {
    long long n = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        n++;
        pos += needle.size();
    }
    return n;
}

int main() {
    const char* path = "/tmp/test_elog_async.log";
    std::remove(path);

    ElogConfig cfg;
    cfg.enableTerminal = false;   // keep test stdout clean
    cfg.enableFile = true;        // file sink ON — so sync would block on fflush
    cfg.logFilePath = path;
    cfg.logLevel = ELOG_LVL_DEBUG;
    if (!elog_init_with_config(cfg)) {
        fprintf(stderr, "[FAIL] elog_init_with_config returned false\n");
        return 1;
    }

    int failures = 0;

    // ---- (c) ORDER: 100 single-thread lines (100 < 128 slots → no overflow) ----
    const int ORD_N = 100;
    for (int i = 0; i < ORD_N; ++i) {
        elog_i("ORD", "ORDMK%06d", i);
    }
    // Let the consumer drain the ORD lines to file BEFORE the bursts below flood
    // the ring and (correctly) drop-oldest evict them. 150 ms > 2 flush intervals.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    // ---- (a) LATENCY: 8 threads × 2000 lines, file sink active ----
    // p99 (not max) is the right gate: a single scheduler-preemption spike says
    // nothing about I/O blocking, whereas in sync mode EVERY call serializes on
    // per-line fwrite+fflush (8-thread contention → 10s of ms per call). p99
    // sub-2 ms under this burst is direct evidence the hot thread isn't blocked.
    const int LAT_T = 8;
    const int LAT_PER = 2000;
    const long long LAT_LOGGED = (long long)LAT_T * LAT_PER;
    {
        std::vector<std::vector<long long>> per_thread(LAT_T);
        std::vector<std::thread> ts;
        auto work = [&](int tid) {
            per_thread[tid].reserve(LAT_PER);
            for (int i = 0; i < LAT_PER; ++i) {
                auto t0 = std::chrono::steady_clock::now();
                elog_i("LAT", "LATMK tid=%d i=%d pad-padding-padding", tid, i);
                auto t1 = std::chrono::steady_clock::now();
                per_thread[tid].push_back(us_between(t0, t1));
            }
        };
        for (int t = 0; t < LAT_T; ++t) ts.emplace_back(work, t);
        for (auto& th : ts) th.join();

        std::vector<long long> all;
        all.reserve(LAT_T * LAT_PER);
        for (auto& v : per_thread) all.insert(all.end(), v.begin(), v.end());
        std::sort(all.begin(), all.end());
        size_t p99_idx = (size_t)(0.99 * all.size());
        if (p99_idx >= all.size()) p99_idx = all.size() - 1;
        long long p99 = all[p99_idx];
        long long mx = all.back();
        printf("[latency] threads=%d per=%d p99_us=%lld max_us=%lld\n",
               LAT_T, LAT_PER, p99, mx);
        if (p99 >= 2000) {
            printf("[FAIL] p99 per-call latency %lld us >= 2000 us (still blocking?)\n", p99);
            failures++;
        }
    }

    // ---- (b) DROP/CNT: single-thread flood, 20k lines → ring(128) overflows ----
    const int FLD_N = 20000;
    const long long FLD_LOGGED = FLD_N;
    long long dropped_before = (long long)elog_async_get_drop_count();  // cumulative so far
    for (int i = 0; i < FLD_N; ++i) {
        elog_i("FLD", "FLDMK %016d", i);
    }
    long long dropped_after_flood = (long long)elog_async_get_drop_count();
    long long flood_delta = dropped_after_flood - dropped_before;
    printf("[flood]   pushed=%lld flood_drops=%lld (cumulative_drops=%lld)\n",
           FLD_LOGGED, flood_delta, dropped_after_flood);
    if (flood_delta <= 0) {
        printf("[FAIL] expected flood drops > 0 after a 20k-line flood (drop-oldest not exercised)\n");
        failures++;
    }

    // ---- (d) DRAIN: final marker must survive deinit ----
    elog_i("TAIL", "TAILMK-MUST-SURVIVE");
    const long long TAIL_LOGGED = 1;
    // Read the FINAL cumulative drop count AFTER the last push (the TAIL push may
    // itself evict one oldest entry) so the accounting invariant is exact.
    long long dropped = (long long)elog_async_get_drop_count();
    elog_deinit_all();  // stop pushes → consumer drains ring → final flush → join

    // ---- read back & verify ----
    std::ifstream f(path);
    if (!f) {
        fprintf(stderr, "[FAIL] cannot open %s after deinit\n", path);
        return 1;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string content = ss.str();

    // (c) ORDER: surviving ORDMK numbers must be strictly increasing; all 100 present
    {
        const std::string mk = "ORDMK";
        size_t pos = 0;
        int last = -1;
        long long cnt = 0;
        bool ordered = true;
        while ((pos = content.find(mk, pos)) != std::string::npos) {
            int v = std::atoi(content.c_str() + pos + mk.size());
            if (v <= last) ordered = false;
            last = v;
            cnt++;
            pos += mk.size();
        }
        printf("[order]   ord_survived=%lld ordered=%d\n", cnt, ordered ? 1 : 0);
        if (cnt != ORD_N) {
            printf("[FAIL] expected %d ORDMK lines, got %lld\n", ORD_N, cnt);
            failures++;
        }
        if (!ordered) {
            printf("[FAIL] ORDMK lines not strictly increasing\n");
            failures++;
        }
    }

    // (d) DRAIN: tail marker present
    {
        bool tail = content.find("TAILMK-MUST-SURVIVE") != std::string::npos;
        printf("[drain]   tail_marker_present=%d\n", tail ? 1 : 0);
        if (!tail) {
            printf("[FAIL] TAILMK marker missing — drain did not flush the tail\n");
            failures++;
        }
    }

    // Hard invariant: written + dropped == logged
    {
        long long w_ord = count_substr(content, "ORDMK");
        long long w_lat = count_substr(content, "LATMK");
        long long w_fld = count_substr(content, "FLDMK");
        long long w_tail = count_substr(content, "TAILMK");
        long long total_written = w_ord + w_lat + w_fld + w_tail;
        long long total_logged = ORD_N + LAT_LOGGED + FLD_LOGGED + TAIL_LOGGED;
        printf("[account] logged=%lld written=%lld dropped=%lld (written+dropped=%lld)\n",
               total_logged, total_written, dropped, total_written + dropped);
        if (total_written + dropped != total_logged) {
            printf("[FAIL] accounting: written(%lld)+dropped(%lld) != logged(%lld)\n",
                   total_written, dropped, total_logged);
            failures++;
        }
    }

    if (failures == 0) {
        printf("ALL PASS\n");
        return 0;
    }
    printf("%d FAIL(S)\n", failures);
    return 1;
}
