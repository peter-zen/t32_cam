#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
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

int main() {
    media::ImageSnapParams params;
    params.setImageSize(1920, 1080);
    media::ImageSnap snap(params);

    std::string outDir = "./sim_sdcard/media";
    Misc::createDirectory(outDir);
    std::string outFile = outDir + "/IMG_" + nowString() + ".JPG";

    bool ok = snap.snap(outFile);
    if (ok) {
        std::cout << "Image generated: " << outFile << std::endl;
        return 0;
    } else {
        std::cerr << "Image generation failed: " << outFile << std::endl;
        return 1;
    }
}
