// T24 golden characterization test for ProductConfig (config.ini -> JSON Phase-2).
//
// Purpose: lock ProductConfig's observable behavior so the BOOT + DEVICE-static
// carve-out from DeviceConfig is provably behavior-preserving. ProductConfig is a
// read-only bag (no set/flush) loaded from product.json (PRODUCT_FILE env).
//
// What is locked (P1-P6 + EQ + T1-T4 + G1):
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
//   T1-T4 (T28 capability presence-set) run via fork+exec self with synthetic
//       fixtures (ProductConfig is a sticky singleton — each case needs a fresh
//       process). T1 = comma parse, T2 = absent -> fail-safe {um_live}, T3 =
//       empty string -> fail-safe, T4 = unknown token retained.
//   G1  (T28 golden, §3.7) res/product.json MUST contain a "capabilities" field
//       (absent = CI red, the production safety net).
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
#include <sys/wait.h>
#include <unistd.h>

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

// ---- T28 capability presence-set cases (T1-T4) ----
// Each runs in a forked child with its own PRODUCT_FILE so the sticky
// ProductConfig singleton loads a fresh fixture per case. The child re-execs
// this binary with "--verify-caps <fixture>"; the parent checks exit code
// (0 = pass). This mirrors the U5-U6 rw-roundtrip fork+exec idiom.

int runCapsVerify(const std::string& fixturePath) {
    // Child body: rebuild singleton from fixturePath, assert caps behavior.
    ::setenv("PRODUCT_FILE", fixturePath.c_str(), 1);
    EnvManager::getInstance()->setEnv("PRODUCT_FILE", fixturePath);
    auto product = ProductConfig::getInstance();
    int fail = 0;

    // Derive the expectation from a convention: the fixture's basename encodes
    // the case. Each fixture is written by the parent (see runCapsChild below).
    std::string base = fixturePath;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);

    if (base == "caps_multi.json") {
        // T1 — "um_live,um_snap" parsed into a set
        if (!product->hasCap("um_live")) { std::cerr << "T1 um_live" << std::endl; ++fail; }
        if (!product->hasCap("um_snap")) { std::cerr << "T1 um_snap" << std::endl; ++fail; }
        if (product->hasCap("um_rec"))   { std::cerr << "T1 um_rec should be absent" << std::endl; ++fail; }
        if (product->hasCap("um_pb"))    { std::cerr << "T1 um_pb should be absent" << std::endl; ++fail; }
        auto caps = product->getCaps();
        if (caps.size() != 2) { std::cerr << "T1 size=" << caps.size() << std::endl; ++fail; }
    } else if (base == "caps_absent.json") {
        // T2 — no capabilities field -> fail-safe {um_live}
        if (!product->hasCap("um_live")) { std::cerr << "T2 um_live fail-safe" << std::endl; ++fail; }
        if (product->hasCap("um_snap"))  { std::cerr << "T2 um_snap should be off" << std::endl; ++fail; }
        if (product->hasCap("um_rec"))   { std::cerr << "T2 um_rec should be off" << std::endl; ++fail; }
        if (product->hasCap("um_pb"))    { std::cerr << "T2 um_pb should be off" << std::endl; ++fail; }
    } else if (base == "caps_empty.json") {
        // T3 — "capabilities": "" -> fail-safe (design §3.7: empty == absent)
        if (!product->hasCap("um_live")) { std::cerr << "T3 um_live fail-safe" << std::endl; ++fail; }
        if (product->hasCap("um_snap"))  { std::cerr << "T3 um_snap should be off" << std::endl; ++fail; }
        auto caps = product->getCaps();
        if (caps.size() != 1) { std::cerr << "T3 size=" << caps.size() << std::endl; ++fail; }
    } else if (base == "caps_unknown.json") {
        // T4 — "um_live,um_bogus": known retained, unknown retained + WARNING
        if (!product->hasCap("um_live"))  { std::cerr << "T4 um_live" << std::endl; ++fail; }
        if (!product->hasCap("um_bogus")) { std::cerr << "T4 um_bogus should be retained (forward-compat)" << std::endl; ++fail; }
        auto caps = product->getCaps();
        if (caps.size() != 2) { std::cerr << "T4 size=" << caps.size() << std::endl; ++fail; }
    } else {
        std::cerr << "unknown caps fixture: " << base << std::endl;
        return 1;
    }
    return fail;
}

// Write a synthetic product.json fixture into a temp file and return its path.
std::string writeCapsFixture(const std::string& tmpDir, const std::string& name,
                             const std::string& capsField) {
    std::string path = tmpDir + "/" + name;
    std::ofstream out(path.c_str());
    out << "{\n";
    if (!capsField.empty()) {
        // capsField is either the raw capabilities value ("um_live,um_snap")
        // or the literal string "ABSENT" / "EMPTY".
        if (capsField == "ABSENT") {
            out << "    \"BOOT\": { \"PModel\": \"T32\" }\n";
        } else if (capsField == "EMPTY") {
            out << "    \"capabilities\": \"\",\n";
            out << "    \"BOOT\": { \"PModel\": \"T32\" }\n";
        } else {
            out << "    \"capabilities\": \"" << capsField << "\",\n";
            out << "    \"BOOT\": { \"PModel\": \"T32\" }\n";
        }
    }
    out << "}\n";
    out.close();
    return path;
}

bool runCapsChild(const std::string& selfBin, const std::string& fixturePath) {
    pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "FAIL: fork for " << fixturePath << std::endl;
        return false;
    }
    if (pid == 0) {
        ::execl(selfBin.c_str(), selfBin.c_str(), "--verify-caps", fixturePath.c_str(),
                (char*)nullptr);
        ::_exit(127);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// G1 — golden: res/product.json must contain a "capabilities" field (production
// safety net, design §3.7: absent => CI red so the fail-safe never fires in
// shipped firmware).
void runGoldenCapsPresentCase() {
    const std::string resPath = std::string(TEST_PROJECT_ROOT) + "/res/product.json";
    std::ifstream f(resPath);
    if (!f.is_open()) {
        require(false, "G1 res/product.json not found at " + resPath);
        return;
    }
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    // Presence of the top-level key. Value correctness is covered by the
    // fixture-driven T1-T4 cases above; this only locks "the field exists".
    bool present = content.find("\"capabilities\"") != std::string::npos;
    require(present, "G1 res/product.json must contain a top-level \"capabilities\" field");
}

}  // namespace

int main(int argc, char* argv[]) {
    // Child mode for T28 caps cases (T1-T4): re-exec'd by the parent with a
    // synthetic fixture. Rebuild the singleton, assert, exit code = result.
    if (argc == 3 && std::strcmp(argv[1], "--verify-caps") == 0) {
        return runCapsVerify(argv[2]);
    }

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

    // T28 — capability cases (T1-T4) via fork+exec so each gets a fresh
    // sticky-singleton process with its own PRODUCT_FILE.
    char selfBuf[4096];
    ssize_t selfLen = ::readlink("/proc/self/exe", selfBuf, sizeof(selfBuf) - 1);
    std::string selfBin = (selfLen > 0) ? std::string(selfBuf, static_cast<size_t>(selfLen))
                                        : std::string(argv[0]);

    // Build temp dir for synthetic fixtures (use build_sim/bin/.test_product_config_tmp).
    std::string tmpDir = std::string(TEST_PROJECT_ROOT) + "/build_sim/bin/.caps_tmp";
    ::mkdir(tmpDir.c_str(), 0755);  // ok if exists

    std::string t1 = writeCapsFixture(tmpDir, "caps_multi.json",  "um_live,um_snap");
    std::string t2 = writeCapsFixture(tmpDir, "caps_absent.json", "ABSENT");
    std::string t3 = writeCapsFixture(tmpDir, "caps_empty.json",  "EMPTY");
    std::string t4 = writeCapsFixture(tmpDir, "caps_unknown.json","um_live,um_bogus");

    require(runCapsChild(selfBin, t1), "T1 caps parse (comma-separated -> set)");
    require(runCapsChild(selfBin, t2), "T2 caps absent -> fail-safe {um_live}");
    require(runCapsChild(selfBin, t3), "T3 caps empty string -> fail-safe {um_live}");
    require(runCapsChild(selfBin, t4), "T4 unknown token retained (forward-compat)");

    // G1 — golden: shipped res/product.json must declare capabilities.
    runGoldenCapsPresentCase();

    if (g_failures != 0) {
        std::cerr << "FAIL: " << g_failures << " assertion(s) failed" << std::endl;
        return 1;
    }

    std::cout << "OK: test_product_config P1-P6 + EQ + T1-T4 + G1 green (product=" << productFixture
              << ", config=" << configFixture << ")" << std::endl;
    return 0;
}
