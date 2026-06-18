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
#include <cstdlib>
#include <sys/stat.h>
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

static void usage(const char* prog) {
    std::cerr << "usage: " << prog << " [dstW=9216] [dstH=5184] [quality=85] [outDir=./media]\n"
              << "       " << prog << " raw [quality=85] [outDir=./media]   (sensor-native, no scale)\n"
              << "  dstW/dstH must be 16-aligned; default ~48M (9216x5184, 16:9).\n";
}

int main(int argc, char** argv) {
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
