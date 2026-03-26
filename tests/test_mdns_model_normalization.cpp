#include "MdnsService.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

bool expectEqual(const std::string& actual, const std::string& expected, const char* caseName) {
    if (actual == expected) {
        return true;
    }

    std::cerr << caseName << " failed: expected [" << expected << "] but got [" << actual << "]" << std::endl;
    return false;
}

} // namespace

int main() {
    if (!expectEqual(service::normalizeMdnsModelValue("CXXX"), "T32", "plain CXXX")) {
        return EXIT_FAILURE;
    }

    if (!expectEqual(service::normalizeMdnsModelValue("  \"CXXX\"  "), "T32", "quoted CXXX")) {
        return EXIT_FAILURE;
    }

    if (!expectEqual(service::normalizeMdnsModelValue(""), "T32", "empty model")) {
        return EXIT_FAILURE;
    }

    if (!expectEqual(service::normalizeMdnsModelValue("T32CamPro"), "T32CamPro", "valid model")) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
