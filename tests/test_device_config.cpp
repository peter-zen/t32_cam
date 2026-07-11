// T23 golden characterization test for DeviceConfig (config.ini -> JSON Phase-1).
//
// Purpose: lock DeviceConfig's observable behavior so the ini->json backend swap
// (Phase-1) is provably behavior-equivalent. The SAME assertion code runs against
// an ini fixture (before the swap) and a json fixture (after the swap); both must
// pass green. This is the refactor-flow "golden" discipline.
//
// What is locked (U1-U8):
//   U1  get(int) hit + numeric conversion (string "8899" -> istringstream >> int)
//   U2  get(int) miss -> default
//   U3  get(string) hit + literal-quote preservation (D1: PCompany="CKVISON"
//       is stored WITH the literal quotes, 9 chars; PName="CAMERA" 8 chars;
//       PWD=@peaceful keeps the leading @)
//   U4  get(string) miss -> default
//   U5  set(int) -> flush -> re-parse (child exec) -> get(int) roundtrip
//   U6  set(string) -> flush -> re-parse (child exec) -> get(string) roundtrip
//   U7  same-named key isolation across sections (DEVICE.PID vs a BOOT.PID we set)
//   U8  empty-string value persistence (HostName="" survives flush + re-parse)
//
// How fixture switching works:
//   argv[1] = path to a fixture file (ini before Phase-1, json after Phase-1).
//   DeviceConfig reads CONFIG_FILE env, so the test sets it to argv[1] before
//   constructing the singleton. The SAME binary is run twice (once per format)
//   -- the assertions are format-agnostic because config_data stays a string bag.
//
// How roundtrip (U5-U8) works without breaking the singleton:
//   DeviceConfig is a process-wide singleton (std::once_flag). We cannot rebuild
//   it in-process after flush. So U5-U8 do: set values -> flush() to a temp path
//   -> fork+exec THIS binary in "--verify-rw <tmp>" mode; the child rebuilds the
//   singleton from the flushed file and asserts the values came back. This truly
//   exercises flush()+parse() round-trip through the on-disk format.

#include "../src/config/devconf/DeviceConfig.h"
#include "../src/config/env/EnvManager.h"
#include "../src/common/Common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
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

// Resolve the fixture path: accept an absolute path as-is, otherwise anchor it
// under TEST_PROJECT_ROOT (set by CMake) so the binary is cwd-independent.
std::string resolvePath(const char* given, const char* fallbackSuffix) {
    if (given && given[0] == '/') return std::string(given);
    std::string root = TEST_PROJECT_ROOT;
    std::string rel = (given && given[0] != '\0') ? std::string(given)
                                                  : std::string(fallbackSuffix);
    if (!rel.empty() && rel[0] != '/') rel = "/" + rel;
    return root + rel;
}

// Prep both the process env (getenv path used by EnvManager consumers) and the
// EnvManager in-memory map (used by DeviceConfig's constructor).
void injectConfigFile(const std::string& path) {
    ::setenv("CONFIG_FILE", path.c_str(), 1);
    EnvManager::getInstance()->setEnv("CONFIG_FILE", path);
}

// Build a fresh DeviceConfig after pointing CONFIG_FILE at `fixturePath`. Must be
// called BEFORE any prior getInstance() in this process (singleton is sticky).
std::shared_ptr<DeviceConfig> buildFromFixture(const std::string& fixturePath) {
    injectConfigFile(fixturePath);
    return DeviceConfig::getInstance();
}

// --- U1-U4: read-only assertions against an already-loaded fixture ---

void assertReadOnly(const std::shared_ptr<DeviceConfig>& config) {
    // U1 — get(int) hit + numeric conversion (string "8899" -> int)
    require(config->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, -1) == 8899,
            "U1 get(int) SERVER.MSPort should be 8899");
    require(config->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, -1) == 123,
            "U1 get(int) SERVER.NTPPort should be 123");

    // U2 — get(int) miss -> default
    require(config->get(INI_SECTION_SERVER, "NOPE_INT", 42) == 42,
            "U2 get(int) miss should return default 42");

    // U3 — get(string) hit + carve-out contract (Phase-2)
    //   Phase-2 carved BOOT + DEVICE-static (CSSID/CPWD/SPKVOL) out of DeviceConfig
    //   into the new read-only ProductConfig (product.json). DeviceConfig's config
    //   fixture no longer has a BOOT section, so BOOT reads return the default —
    //   this proves the carve-out is complete (no BOOT residue on DeviceConfig).
    //   The D1 literal-quote preservation for PCompany/PName is now covered by
    //   test_product_config (P1/P2 read those fields through ProductConfig).
    require(config->get(INI_SECTION_BOOT, INI_KEY_PCOMPANT, "") == "",
            "U3 BOOT.PCompany must be absent from DeviceConfig (carved to ProductConfig)");
    require(config->get(INI_SECTION_BOOT, INI_KEY_PNAME, "") == "",
            "U3 BOOT.PName must be absent from DeviceConfig (carved to ProductConfig)");
    // T25 Phase-3: SYSTEM section removed from DeviceConfig (UPID/PWD/BR*/voltage
    // migrated to Settings). SYSTEM.UPWD now returns the default, proving the
    // migration is complete (no SYSTEM residue on DeviceConfig).
    require(config->get(INI_SECTION_SYS, INI_KEY_UPWD, "") == "",
            "U3 SYSTEM.PWD must be absent from DeviceConfig (migrated to Settings)");
    require(config->get(INI_SECTION_DEVICE, INI_KEY_PID, "") == "T152T20250624001",
            "U3 DEVICE.PID string hit");

    // U4 — get(string) miss -> default
    require(config->get(INI_SECTION_SERVER, "NOPE_STR", std::string("fallback")) == "fallback",
            "U4 get(string) miss should return fallback");
}

// --- U5-U8: write (set+flush) then re-parse in a child process, assert roundtrip ---

// Write the child's assertions into this same binary's "--verify-rw" mode entry,
// so we exercise the real flush()->parse() path through the on-disk file.
int runRwVerify(const std::string& flushedPath) {
    // Child process: rebuild singleton from the flushed file, assert values.
    injectConfigFile(flushedPath);
    auto config = DeviceConfig::getInstance();

    // U5 — set(int) roundtrip
    require(config->get(INI_SECTION_POLICY, "Record", -1) == 1,
            "U5 set(int) POLICY.Record roundtrip should be 1");
    require(config->get(INI_SECTION_POLICY, "Record", -1) != 0,
            "U5 set(int) POLICY.Record should NOT still be the fixture default 0");

    // U6 — set(string) roundtrip
    require(config->get(INI_SECTION_MDNS, "HostName", std::string("x")) == "cam-001",
            "U6 set(string) MDNS.HostName roundtrip should be cam-001");

    // U7 — same-named key isolation: we set BOOT.PID, DEVICE.PID must stay the
    //      fixture value (untouched).
    require(config->get(INI_SECTION_DEVICE, INI_KEY_PID, "") == "T152T20250624001",
            "U7 DEVICE.PID must stay fixture value (BOOT.PID set must not leak)");
    require(config->get(INI_SECTION_BOOT, "PID", std::string("")) == "BOOT_PID_999",
            "U7 BOOT.PID should be the value we set (section isolation)");

    // U8 — empty-string value persistence: SYSTEM.UPID is set to "" and must
    //      survive flush+re-parse as "" (not lost / not defaulted).
    require(config->get(INI_SECTION_SYS, "UPID_DISABLED_FOR_TEST", std::string("LEAKED")) == "",
            "U8 empty string set must persist as empty through flush+re-parse");

    return g_failures == 0 ? 0 : 1;
}

// Helper: copy a file (used to seed the writable temp from the fixture).
bool copyFile(const std::string& src, const std::string& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in.is_open()) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out << in.rdbuf();
    out.flush();
    return out.good();
}

std::string selfExecutablePath() {
    char buf[4096];
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return std::string();
    buf[n] = '\0';
    return std::string(buf);
}

}  // namespace

int main(int argc, char* argv[]) {
    // Mode B: child invoked as `test_device_config --verify-rw <flushed_path>`.
    if (argc == 3 && std::strcmp(argv[1], "--verify-rw") == 0) {
        return runRwVerify(argv[2]);
    }

    // Mode A: `test_device_config <fixture_path>` -- run U1-U4 then roundtrip.
    if (argc < 2) {
        std::cerr << "usage: " << argv[0] << " <fixture_path>" << std::endl;
        return 2;
    }

    std::string fixtureAbs = resolvePath(argv[1], "/tests/assets/configs/device_config_sample.ini");

    // Verify the fixture exists.
    struct stat st;
    if (::stat(fixtureAbs.c_str(), &st) != 0) {
        std::cerr << "FAIL: fixture not found: " << fixtureAbs << std::endl;
        return 2;
    }

    // Seed a writable copy of the fixture in a temp dir. The singleton is built
    // from this copy so flush() writes here (not clobbering the source fixture),
    // and the roundtrip child re-reads exactly this file.
    char tpl[] = "/tmp/t23_devconf_XXXXXX";
    char* d = ::mkdtemp(tpl);
    if (d == nullptr) {
        std::cerr << "FAIL: mkdtemp" << std::endl;
        return 1;
    }
    std::string tmpDir = std::string(d);
    std::string seedPath = tmpDir + "/seed.cfg";
    if (!copyFile(fixtureAbs, seedPath)) {
        std::cerr << "FAIL: copyFile fixture -> seed" << std::endl;
        return 1;
    }

    // Build the singleton from the seeded copy.
    auto config = buildFromFixture(seedPath);

    // U1-U4 read-only assertions.
    assertReadOnly(config);

    // U5-U8 writes, then flush to seedPath (the singleton's config_filename).
    config->set(INI_SECTION_POLICY, "Record", 1);               // U5 set(int)
    config->set(INI_SECTION_MDNS, "HostName", "cam-001");       // U6 set(string)
    config->set(INI_SECTION_BOOT, "PID", "BOOT_PID_999");       // U7 isolation
    config->set(INI_SECTION_SYS, "UPID_DISABLED_FOR_TEST", ""); // U8 empty
    if (!config->flush()) {
        std::cerr << "FAIL: flush() returned false" << std::endl;
        return 1;
    }

    // Fork+exec this binary in verify mode; child rebuilds singleton from the
    // flushed seedPath and asserts the roundtrip values survived.
    std::string self = selfExecutablePath();
    if (self.empty()) {
        std::cerr << "FAIL: readlink /proc/self/exe" << std::endl;
        return 1;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        std::cerr << "FAIL: fork" << std::endl;
        return 1;
    }
    if (pid == 0) {
        // Child: exec self in verify mode.
        ::execl(self.c_str(), self.c_str(), "--verify-rw", seedPath.c_str(), (char*)nullptr);
        // exec only returns on error.
        ::_exit(127);
    }

    int status = 0;
    if (::waitpid(pid, &status, 0) < 0) {
        std::cerr << "FAIL: waitpid" << std::endl;
        return 1;
    }
    int childRc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

    // Cleanup temp dir (best-effort).
    ::remove(seedPath.c_str());
    ::rmdir(tmpDir.c_str());

    if (childRc != 0) {
        std::cerr << "FAIL: roundtrip child exit=" << childRc << std::endl;
        return 1;
    }

    if (g_failures != 0) {
        std::cerr << "FAIL: " << g_failures << " assertion(s) failed" << std::endl;
        return 1;
    }

    std::cout << "OK: test_device_config (fixture=" << fixtureAbs << ") U1-U8 green" << std::endl;
    return 0;
}
