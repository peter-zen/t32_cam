// T24 golden characterization test for ProductConfig (config.ini -> JSON Phase-2).
//
// Purpose: lock ProductConfig's observable behavior so the BOOT + DEVICE-static
// carve-out from DeviceConfig is provably behavior-preserving. ProductConfig is a
// read-only bag (no set/flush) loaded from product.json (PRODUCT_FILE env).
//
// What is locked (P1-P6 + EQ):
//   P1  get(int) hit on BOOT.PType (product.json stores "1" -> int 1)
//   P2  get(string) hit on BOOT.PModel ("T32") + D1 literal-quote preservation
//       on PName ("\"CAMERA\"" stored with literal quotes)
//   P3  get(string) hit on DEVICE.CSSID (DEVICE-static now lives in product.json)
//   P4  get(int) miss -> default
//   P5  get(DEVICE, PID, "") -> "" (PID is NOT in product.json — it stays on
//       DeviceConfig as the MCU-authoritative, runtime-rewritable field)
//   P6  read-only structural enforcement: ProductConfig.h has no `void set` or
//       `bool flush` method (grep the header at runtime)
//   EQ  equivalence snapshot across ProductConfig + DeviceConfig:
//       - ProductConfig PModel == "T32"
//       - ProductConfig CSSID  == "CKV"
//       - DeviceConfig   PID   == "T152T20250624001"  (PID stays DeviceConfig)
//       Proves no field was lost and no value drifted in the carve-out.
//
// How the test boots ProductConfig:
//   ProductConfig reads PRODUCT_FILE env. The test sets it to the fixture before
//   constructing the singleton. DeviceConfig reads CONFIG_FILE; the EQ case sets
//   both so both singletons load their respective fixtures.

#include "../src/config/devconf/DeviceConfig.h"
#include "../src/config/devconf/ProductConfig.h"
#include "../src/config/env/EnvManager.h"
#include "../src/common/Common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>

namespace {

int g_failures = 0;

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        ++g_failures;
    }
}

std::string resolvePath(const char* given, const char* fallbackSuffix) {
    if (given && given[0] == '/') return std::string(given);
    std::string root = TEST_PROJECT_ROOT;
    std::string rel = (given && given[0] != '\0') ? std::string(given)
                                                  : std::string(fallbackSuffix);
    if (!rel.empty() && rel[0] != '/') rel = "/" + rel;
    return root + rel;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Build ProductConfig singleton from a fixture by injecting PRODUCT_FILE before
// first getInstance(). Must be called before any prior getInstance() in-process.
std::shared_ptr<ProductConfig> buildProductFromFixture(const std::string& productFixturePath) {
    ::setenv("PRODUCT_FILE", productFixturePath.c_str(), 1);
    EnvManager::getInstance()->setEnv("PRODUCT_FILE", productFixturePath);
    return ProductConfig::getInstance();
}

void runReadOnlyCases(const std::shared_ptr<ProductConfig>& product) {
    // P1 — get(int) hit on BOOT.PType
    require(product->get(INI_SECTION_BOOT, INI_KEY_PTYPE, -1) == 1,
            "P1 get(int) BOOT.PType should be 1");

    // P2 — get(string) hit on BOOT.PModel + D1 literal-quote on PName
    require(product->get(INI_SECTION_BOOT, INI_KEY_PMODEL, "") == "T32",
            "P2 get(string) BOOT.PModel should be T32");
    require(product->get(INI_SECTION_BOOT, INI_KEY_PNAME, "") == "\"CAMERA\"",
            "P2 BOOT.PName must keep literal double-quotes (D1 preserved)");

    // P3 — get(string) hit on DEVICE.CSSID (DEVICE-static carved to product.json)
    require(product->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "") == "CKV",
            "P3 get(string) DEVICE.CSSID should be CKV");
    require(product->get(INI_SECTION_DEVICE, INI_KEY_CPWD, "") == "ckvison6688",
            "P3 get(string) DEVICE.CPWD should be ckvison6688");
    require(product->get(INI_SECTION_DEVICE, INI_KEY_SPKVOL, -1) == 0,
            "P3 get(int) DEVICE.SPKVOL should be 0");

    // P4 — get(int) miss -> default
    require(product->get(INI_SECTION_BOOT, "NOPE_INT", 42) == 42,
            "P4 get(int) miss should return default 42");
    require(product->get(INI_SECTION_DEVICE, "NOPE_STR", std::string("fallback")) == "fallback",
            "P4 get(string) miss should return fallback");

    // P5 — PID is NOT in product.json (stays on DeviceConfig as MCU source).
    //      This proves the carve-out kept PID on the writable side.
    require(product->get(INI_SECTION_DEVICE, INI_KEY_PID, "") == "",
            "P5 DEVICE.PID must be absent from product.json (stays DeviceConfig)");
}

void runReadOnlyStructCase() {
    // P6 — ProductConfig is read-only by construction: no set/flush method.
    // Grep the header for `void set` / `bool flush` — both must be absent.
    const std::string headerPath = std::string(TEST_PROJECT_ROOT) +
                                   "/src/config/devconf/ProductConfig.h";
    if (!fileExists(headerPath)) {
        require(false, "P6 ProductConfig.h not found at " + headerPath);
        return;
    }
    std::string cmd = "grep -cE '\\b(void set|bool flush)\\b' '" + headerPath + "' 2>/dev/null || true";
    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        require(false, "P6 popopen failed");
        return;
    }
    char buf[64];
    std::string out;
    while (::fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
    ::pclose(pipe);
    // Strip whitespace.
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) {
        out.pop_back();
    }
    require(out == "0",
            "P6 ProductConfig.h must have no `void set` / `bool flush` (read-only by construction), got count=" + out);
}

void runEquivalenceCase(const std::string& productFixturePath,
                        const std::string& configFixturePath) {
    // Both singletons must already be built (sticky singletons); the caller
    // injects both env paths before this runs. Here we just assert values.
    auto product = ProductConfig::getInstance();
    auto deviceConfig = DeviceConfig::getInstance();

    // EQ — cross-config snapshot: no field lost, no value drifted.
    require(product->get(INI_SECTION_BOOT, INI_KEY_PMODEL, "") == "T32",
            "EQ ProductConfig BOOT.PModel should be T32");
    require(product->get(INI_SECTION_DEVICE, INI_KEY_CSSID, "") == "CKV",
            "EQ ProductConfig DEVICE.CSSID should be CKV");
    require(deviceConfig->get(INI_SECTION_DEVICE, INI_KEY_PID, "") == "T152T20250624001",
            "EQ DeviceConfig DEVICE.PID should stay T152T20250624001 (MCU-authoritative)");
    require(deviceConfig->get(INI_SECTION_BOOT, INI_KEY_PMODEL, "") == "",
            "EQ DeviceConfig BOOT.PModel must be absent (carved to ProductConfig)");
}

}  // namespace

int main(int argc, char* argv[]) {
    // argv[1] = product fixture (default tests/assets/configs/product_sample.json)
    // argv[2] = config fixture for EQ (default tests/assets/configs/device_config_sample.json)
    std::string productFixture = resolvePath(argv[1], "/tests/assets/configs/product_sample.json");
    std::string configFixture  = resolvePath(argv[2], "/tests/assets/configs/device_config_sample.json");

    if (!fileExists(productFixture)) {
        std::cerr << "FAIL: product fixture not found: " << productFixture << std::endl;
        return 2;
    }
    if (!fileExists(configFixture)) {
        std::cerr << "FAIL: config fixture not found: " << configFixture << std::endl;
        return 2;
    }

    // Inject BOTH env paths before constructing either singleton. ProductConfig
    // and DeviceConfig are process-wide sticky singletons.
    ::setenv("PRODUCT_FILE", productFixture.c_str(), 1);
    EnvManager::getInstance()->setEnv("PRODUCT_FILE", productFixture);
    ::setenv("CONFIG_FILE", configFixture.c_str(), 1);
    EnvManager::getInstance()->setEnv("CONFIG_FILE", configFixture);

    auto product = ProductConfig::getInstance();
    auto deviceConfig = DeviceConfig::getInstance();  // construct for EQ
    (void)deviceConfig;

    runReadOnlyCases(product);
    runReadOnlyStructCase();
    runEquivalenceCase(productFixture, configFixture);

    if (g_failures != 0) {
        std::cerr << "FAIL: " << g_failures << " assertion(s) failed" << std::endl;
        return 1;
    }

    std::cout << "OK: test_product_config P1-P6 + EQ green (product=" << productFixture
              << ", config=" << configFixture << ")" << std::endl;
    return 0;
}
