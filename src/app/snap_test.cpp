// snap_test — large-image capture feasibility tool.
//
//   snap_test [dstW=9216] [dstH=5184] [quality=85] [outDir=./media]   strip+scale (~48M)
//   snap_test raw [quality=85] [outDir=./media]                       sensor-native, NO scale
//
// "raw" captures one sensor-native (2560x1440) JPEG via GetFrameEx + InputJpege
// with NO upscaling/stripping — validates the base fetch+encode pipeline
// independent of strip stitching and SIMD resize.
//
// Default 9216x5184 interpolates sensor 2560x1440 up 3.6x via strip stitching.
// dstW/dstH must be 16-aligned. Hardware only (simulation -> FAIL).

#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <sys/time.h>
#include "ImageSnap.h"
#include "Common.h"
#include "misc/Misc.h"

static std::string nowString() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

static long fileSize(const std::string& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return -1;
    return static_cast<long>(st.st_size);
}

// On a fresh boot the RTC is 1970 and creating a file with a pre-1980 mtime
// triggers a kernel FAT oops on the SD card (confirmed 2026-06-22: snap's
// fopen -> "Unable to handle kernel paging request"). Production apps sync time
// via ProcessLifecycle::syncSystemTime; snap_test is standalone, so force a
// plausible clock if the current one is pre-2026 (keeps filenames FAT-valid).
static void ensurePlausibleClock() {
    time_t now = std::time(nullptr);
    struct tm tmv;
    if (localtime_r(&now, &tmv) && (tmv.tm_year + 1900) < 2026) {
        struct tm fixed = {};
        fixed.tm_year = 126;       // 2026
        fixed.tm_mon = 5;          // June (0-based)
        fixed.tm_mday = 22;
        fixed.tm_hour = 12;
        struct timeval tv;
        tv.tv_sec = std::mktime(&fixed);
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
        std::cerr << "[snap_test] system clock was implausible (<2026); set to 2026-06-22"
                  << std::endl;
    }
}

static void usage(const char* prog) {
    std::cerr << "usage: " << prog << " [dstW=9216] [dstH=5184] [quality=85] [outDir=./media]\n"
              << "       " << prog << " raw [quality=85] [outDir=./media]   (sensor-native, no scale)\n"
              << "  dstW/dstH must be 16-aligned; default ~48M (9216x5184, 16:9).\n";
}

// Unified snap mode: exercises the production ImageSnap::snap() path (auto
// ≤8M HW-scaler vs >8M strip; >8M burst uses the temp-buffer flow). Supports
// variable main resolution, burst count, and optional thumbnail — the full snap
// spec. Emits a machine-parseable result anchor for tests/host/test_snap_smoke.py.
//
//   snap_test snap <W> <H> [quality=85] [count=1] [--no-thumb] [outDir=./media]
static int runSnapMode(int argc, char** argv) {
    bool noThumb = false;
    std::vector<std::string> pos;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--no-thumb") { noThumb = true; continue; }
        pos.push_back(a);
    }
    if (pos.size() < 2) {
        std::cerr << "usage: snap_test snap <W> <H> [quality=85] [count=1] [--no-thumb] [outDir=./media]\n";
        return 2;
    }
    int dstW = std::atoi(pos[0].c_str());
    int dstH = std::atoi(pos[1].c_str());
    int quality = (pos.size() > 2) ? std::atoi(pos[2].c_str()) : 85;
    int count = (pos.size() > 3) ? std::atoi(pos[3].c_str()) : 1;
    std::string outDir = (pos.size() > 4) ? pos[4] : "./media";
    if (dstW <= 0 || dstH <= 0 || quality < 1 || quality > 99 || count < 1 || count > 20) {
        std::cerr << "ERR: invalid snap args (W/H/quality/count)\n";
        return 2;
    }
    // large = software-scale path (target > sensor-native 2560x1440); matches
    // ImageSnap's routing. snap_test always sets sensorNative=2560x1440 below.
    ensurePlausibleClock();
    bool large = (dstW > 2560 || dstH > 1440);

    Misc::createDirectory(outDir);
    std::string ts = nowString();
    std::vector<std::string> files;
    for (int i = 0; i < count; i++) {
        std::ostringstream name;
        name << outDir << "/IMG_" << ts << "_" << (i + 1) << "_" << dstW << "x" << dstH << ".JPG";
        files.push_back(name.str());
    }

    std::cout << "[snap_test] mode=snap target=" << dstW << "x" << dstH
              << " quality=" << quality << " count=" << count
              << " thumb=" << (noThumb ? "off" : "on")
              << " large=" << (large ? 1 : 0) << std::endl;

    media::ImageSnapParams params;
    params.setImageSize(dstW, dstH);
    params.setSensorNativeSize(2560, 1440);
    params.setThumbnailEnabled(!noThumb);
    media::ImageSnap snap(params);

    auto t0 = std::chrono::steady_clock::now();
    bool ok = snap.snap(files);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    // Verify the produced JPEGs.
    int filesOk = 0;
    long minBytes = -1, maxBytes = -1;
    for (auto& f : files) {
        long sz = fileSize(f);
        if (sz > 0) {
            filesOk++;
            if (minBytes < 0 || sz < minBytes) minBytes = sz;
            if (sz > maxBytes) maxBytes = sz;
        }
    }

    // Persist the thumbnail next to the first JPEG when captured (verifiable artifact).
    std::string thumbState = noThumb ? "off" : "on";
    if (!noThumb && ok && snap.hasThumbnail() && filesOk > 0 && !snap.getThumbnailData().empty()) {
        std::string thumbPath = files[0] + ".thumb.jpg";
        FILE* tf = fopen(thumbPath.c_str(), "wb");
        if (tf) {
            const auto& td = snap.getThumbnailData();
            fwrite(td.data(), 1, td.size(), tf);
            fclose(tf);
            thumbState = "saved";
        }
    }

    bool pass = ok && filesOk == count;
    std::cout << "[snap_test] result=" << (pass ? "OK" : "FAIL")
              << " mode=snap size=" << dstW << "x" << dstH
              << " large=" << (large ? 1 : 0)
              << " count=" << count
              << " thumb=" << thumbState
              << " files=" << filesOk
              << " minbytes=" << minBytes
              << " maxbytes=" << maxBytes
              << " total=" << ms << "ms" << std::endl;
    return pass ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "snap") {
        return runSnapMode(argc, argv);
    }
    if (argc > 1) {
        std::string a1 = argv[1];
        if (a1 == "-h" || a1 == "--help") { usage(argv[0]); return 0; }
    }

    bool raw = (argc > 1 && std::string(argv[1]) == "raw");
    int ab = raw ? 2 : 1;
    int dstW = raw ? 2560 : ((argc > ab) ? std::atoi(argv[ab]) : 9216);
    int dstH = raw ? 1440 : ((argc > ab + 1) ? std::atoi(argv[ab + 1]) : 5184);
    int quality = (argc > ab + 2) ? std::atoi(argv[ab + 2]) : 85;
    std::string outDir = (argc > ab + 3) ? argv[ab + 3] : "./media";

    if (!raw && (dstW <= 0 || dstH <= 0 || (dstW % 16) != 0 || (dstH % 16) != 0)) {
        std::cerr << "ERR: dst " << dstW << "x" << dstH << " must be 16-aligned\n";
        usage(argv[0]);
        return 2;
    }
    if (quality < 1 || quality > 99) {
        std::cerr << "ERR: quality " << quality << " out of range [1,99]\n";
        return 2;
    }

    Misc::createDirectory(outDir);
    std::ostringstream name;
    name << outDir << "/IMG_" << nowString() << "_" << dstW << "x" << dstH << ".JPG";
    std::string outFile = name.str();

    std::cout << "[snap_test] " << (raw ? "RAW" : "STRIP") << " target=" << dstW << "x" << dstH
              << " quality=" << quality << " out=" << outFile << std::endl;

    media::ImageSnapParams params;
    params.setImageSize(dstW, dstH);
    params.setSensorNativeSize(2560, 1440);
    media::ImageSnap snap(params);

    auto t0 = std::chrono::steady_clock::now();
    bool ok = snap.snapLargeStrip(outFile, quality, raw);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    long sz = ok ? fileSize(outFile) : -1;
    std::cout << "[snap_test] result=" << (ok ? "OK" : "FAIL")
              << " file=" << outFile << " size=" << sz << "B"
              << " total=" << ms << "ms" << std::endl;
    return ok ? 0 : 1;
}
