// htc_wifi_app — standalone WiFi connection tool.
//
// Layered design (planner T6 §3):
//   main() (this file)              -> CLI parse + orchestration + exit codes + logging
//   wifi_app_logic (pure, no syscall) -> Decision / write-back gate / exit-code mapping
//   wifi_reconnect                  -> graceful SSID switch (no wpa_supplicant restart)
//   Misc / MCU                      -> reused as-is (connectWifi / startDHCP / isWifi* /
//                                      readUPID/readUPWD/writeUPID/writeUPWD)
//
// This binary is a "connect once per invocation" foreground tool (no daemon).
// Credential sources are ONLY CLI args OR MCU registers (readUPID/readUPWD);
// it deliberately does NOT read any ini file, so it is unaffected by the
// Common.h INI_KEY_UPWD="PWD" naming pitfall.
//
// Exit code contract (stable, planner §4):
//   0 success / 2 driver load fail / 3 connect fail / 4 DHCP fail
//   5 connected OK but MCU write-back gated/skipped / 6 arg or credential error

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <functional>
#include <unistd.h>

#include "Misc.h"
#include "MCU.h"
#include "Logger.h"

#include "wifi_app_logic.h"
#include "wifi_reconnect.h"

using namespace wifi_app_logic;

namespace {

// IIC reads (readUPID/readUPWD) are single-shot with no retry at the IIC layer
// (IIC.cpp:63-78: one ::read() syscall, returns <=0 -> caller sees ""). The MCU
// registers themselves are reliable (htc_mcu_api_test proves writeUPID persists
// across reboot and readUPID reads it back cleanly), so a transient bus read
// failure is recoverable by retrying. This is a CALLER-side retry — MCU.cpp is
// left untouched. Returns the first non-empty result, or "" after <retries>.
std::string readMcuStrWithRetry(const std::function<std::string()> &readFn,
                                const char *label, int retries = 3)
{
    for (int i = 0; i < retries; ++i) {
        std::string v = readFn();
        if (!v.empty()) {
            return v;
        }
        Logger::log(LogLevel::WARNING,
                    "%s read empty (attempt %d/%d) — transient I2C read, retrying",
                    label, i + 1, retries);
        fprintf(stderr, "[DEBUG] %s read empty (attempt %d/%d), retrying\n",
                label, i + 1, retries);
        usleep(50 * 1000); // 50ms between attempts
    }
    return "";
}


struct CliArgs {
    std::string ifname   = "wlan0";
    std::string ssid;        // empty -> read from MCU
    std::string password;    // empty -> read from MCU
    bool        haveCliSsid = false;
    bool        writeMcu    = false;
    bool        noDhcp      = false;
    bool        verbose     = false;
    bool        help        = false;
};

void printUsage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [options]\n"
        "  Connect the device to a WiFi network and (optionally) persist the\n"
        "  credentials to the MCU. Credentials come from --ssid/--pwd or, if\n"
        "  omitted, from the MCU registers (readUPID/readUPWD).\n"
        "\n"
        "Options:\n"
        "  --ssid <SSID>       Target SSID (default: read from MCU).\n"
        "  --pwd <PASSWORD>    Target password (default: read from MCU).\n"
        "  --no-dhcp           Skip DHCP after connecting (DHCP is on by default).\n"
        "  --write-mcu         Persist SSID/password to MCU after a successful\n"
        "                      connection (strictly gated; see exit code 5).\n"
        "  --if <name>         WLAN interface name (default: wlan0).\n"
        "  -v, --verbose       Verbose logging.\n"
        "  -h, --help          Show this help and exit.\n"
        "\n"
        "Exit codes:\n"
        "  0  success\n"
        "  2  driver load failure\n"
        "  3  connection failure\n"
        "  4  DHCP failure\n"
        "  5  connected OK but MCU write-back was skipped (gated)\n"
        "  6  argument / credential error\n",
        prog);
}

// Minimal long-option parser. Returns false on parse error (caller exits 6).
bool parseArgs(int argc, char **argv, CliArgs &out, std::string &err)
{
    err.clear();
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](const char *name) -> std::string {
            if (i + 1 >= argc) {
                err = std::string("missing value for ") + name;
                return std::string();
            }
            return std::string(argv[++i]);
        };

        if (a == "-h" || a == "--help") {
            out.help = true;
        } else if (a == "-v" || a == "--verbose") {
            out.verbose = true;
        } else if (a == "--no-dhcp") {
            out.noDhcp = true;
        } else if (a == "--write-mcu") {
            out.writeMcu = true;
        } else if (a == "--if") {
            out.ifname = next("--if");
            if (out.ifname.empty() && err.empty()) {
                err = "empty value for --if";
            }
        } else if (a == "--ssid") {
            out.ssid = next("--ssid");
            if (err.empty()) {
                out.haveCliSsid = true;
            }
        } else if (a == "--pwd") {
            out.password = next("--pwd");
        } else if (a.rfind("--", 0) == 0) {
            err = "unknown option: " + a;
            return false;
        } else {
            err = "unexpected positional argument: " + a;
            return false;
        }
        if (!err.empty()) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    CliArgs args;
    std::string parseErr;
    if (!parseArgs(argc, argv, args, parseErr)) {
        fprintf(stderr, "htc_wifi_app: %s\n", parseErr.c_str());
        printUsage(stderr, argv[0]);
        return EXIT_ARG_ERROR;
    }
    if (args.help) {
        printUsage(stdout, argv[0]);
        return EXIT_OK;
    }

    Logger::log(LogLevel::INFO,
                "htc_wifi_app start: if=%s writeMcu=%d dhcp=%s",
                args.ifname.c_str(), (int)args.writeMcu,
                args.noDhcp ? "off" : "on");

    // ---- Resolve target credentials (CLI takes precedence; else MCU). ----
    // Requirement 4: no args -> use MCU SSID/password.
    // Requirement 2: --ssid/--pwd -> use CLI values.
    std::string targetSsid = args.ssid;
    std::string targetPwd  = args.password;
    if (!args.haveCliSsid) {
        // Read from MCU registers. readUPID()/readUPWD() return "" on failure
        // (sim IIC bypass / bus error / transient I2C read). Retry a few times
        // because the IIC layer is single-shot no-retry; the register itself is
        // reliable. Empty SSID after retries -> abort with code 6.
        auto mcu = MCU::getInstance();
        targetSsid = readMcuStrWithRetry([&mcu]{ return mcu->readUPID(); }, "UPID");
        targetPwd  = readMcuStrWithRetry([&mcu]{ return mcu->readUPWD(); }, "UPWD");
        fprintf(stderr, "[DEBUG] MCU creds: UPID=<<%s>> UPWD=<<%s>>\n",
                targetSsid.c_str(), targetPwd.c_str());
        Logger::log(LogLevel::INFO,
                    "No --ssid given; using MCU credentials (UPID len=%zu, UPWD len=%zu)",
                    targetSsid.size(), targetPwd.size());
    }

    Target target;
    target.ssid = targetSsid;
    target.password = targetPwd;
    target.hasCredentials = !targetSsid.empty();

    // ---- Decide what to do based on current link state. ----
    LinkState state;
    state.connected = Misc::isWifiConnected(args.ifname);
    state.currentSsid = wifi_reconnect::currentSSID(args.ifname);

    Logger::log(LogLevel::INFO,
                "link: connected=%d currentSSID='%s' targetSSID='%s'",
                (int)state.connected, state.currentSsid.c_str(),
                target.ssid.c_str());
    // [DEBUG] visible on the terminal so we can see exactly what currentSSID()
    // returned (delimiters << >> expose trailing whitespace/newlines).
    fprintf(stderr, "[DEBUG] decide: connected=%d currentSSID=<<%s>> (len=%zu) "
                    "target=<<%s>> (len=%zu)\n",
            (int)state.connected, state.currentSsid.c_str(),
            state.currentSsid.size(), target.ssid.c_str(), target.ssid.size());

    Decision d = decide(state, target);
    fprintf(stderr, "[DEBUG] decision=%d\n", (int)d);
    switch (d) {
        case ABORT: {
            // No usable credentials.
            Logger::log(LogLevel::ERROR,
                        "No usable target SSID (empty). Aborting (exit 6).");
            fprintf(stderr,
                "htc_wifi_app: no target SSID available. "
                "Pass --ssid/--pwd or populate MCU registers.\n");
            return EXIT_ARG_ERROR;
        }
        case FRESH_CONNECT: {
            // Not connected at all -> Misc::connectWifi handles driver-load
            // idempotency (insmod + re-check) and first-connect supplicant
            // spawn (Requirement 1).
            Logger::log(LogLevel::INFO, "Not connected; calling connectWifi (fresh).");
            bool driverLoaded = Misc::isWifiDriverLoaded();
            Logger::log(LogLevel::INFO, "driver_loaded=%d", (int)driverLoaded);
            if (!Misc::connectWifi(target.ssid, target.password)) {
                // connectWifi insmod re-check failure -> driver problem (2);
                // otherwise a connect failure (3). Distinguish by re-probing
                // the driver: if it is loaded, the failure was the association.
                if (!Misc::isWifiDriverLoaded()) {
                    Logger::log(LogLevel::ERROR, "Driver not loaded after connectWifi (exit 2).");
                    return EXIT_DRIVER_FAIL;
                }
                Logger::log(LogLevel::ERROR, "connectWifi failed (exit 3).");
                return EXIT_CONNECT_FAIL;
            }
            break;
        }
        case RECONNECT: {
            // Connected but to a different SSID -> graceful switch WITHOUT
            // restarting wpa_supplicant (preserves T5 fix).
            Logger::log(LogLevel::INFO,
                        "Connected to different SSID; graceful switch (no supplicant restart).");
            if (!wifi_reconnect::reconnectSSID(args.ifname, target.ssid, target.password)) {
                Logger::log(LogLevel::ERROR, "Graceful reconnect failed (exit 3).");
                return EXIT_CONNECT_FAIL;
            }
            break;
        }
        case REUSE: {
            // Already on the target SSID -> nothing to do for the link.
            Logger::log(LogLevel::INFO, "Already connected to target SSID; reusing.");
            break;
        }
    }

    // ---- Verify L2 association to the target SSID (BEFORE DHCP). ----
    // NOTE: Misc::isWifiConnected() is an L3 check (IP + gateway non-empty); it
    // is false until DHCP runs. Using it here made a successful fresh association
    // look "not connected" and bail (exit 3) before DHCP ever ran — leaving wlan0
    // associated but with no IP (real-hw bug found on T32). DHCP (which produces
    // the IP) runs in the next block, so judge by L2 here: associated iff
    // currentSSID() is non-empty and equals the target SSID.
    std::string liveSsid = wifi_reconnect::currentSSID(args.ifname);
    fprintf(stderr, "[DEBUG] post-probe: liveSSID=<<%s>> (len=%zu) target=<<%s>>\n",
            liveSsid.c_str(), liveSsid.size(), target.ssid.c_str());
    if (liveSsid.empty() || liveSsid != target.ssid) {
        Logger::log(LogLevel::ERROR,
                    "Post-connect probe: not associated to target "
                    "(liveSSID='%s' target='%s') (exit 3).",
                    liveSsid.c_str(), target.ssid.c_str());
        return EXIT_CONNECT_FAIL;
    }

    // ---- DHCP (Requirement 1 tail: default DHCP after connecting). ----
    if (!args.noDhcp) {
        if (!Misc::startDHCP(args.ifname)) {
            Logger::log(LogLevel::ERROR, "DHCP failed (exit 4).");
            return EXIT_DHCP_FAIL;
        }
        // Re-probe after DHCP: IP/gateway should now be present (isWifiConnected
        // already gates on both). Keep the live SSID read from before DHCP.
    }

    // ---- MCU write-back (Requirement 3: ONLY after success, strict gate). ----
    // Gate requires BOTH isWifiConnected()==true AND currentSSID()==target,
    // AND a fresh re-read of currentSSID immediately before writing (defends
    // against "connected but landed on another SSID").
    if (args.writeMcu) {
        std::string freshSsid = wifi_reconnect::currentSSID(args.ifname);
        bool stillConnected = Misc::isWifiConnected(args.ifname);
        bool allow = mayWriteBack(stillConnected, freshSsid, target.ssid);
        // [DEBUG] visible on terminal: shows why the gate passes/fails. IP and
        // gateway are isWifiConnected's two sub-conditions; a missing gateway
        // right after udhcpc is a common false-negative.
        fprintf(stderr,
                "[DEBUG] write-gate: stillConnected=%d ip=<<%s>> gw=<<%s>> "
                "freshSSID=<<%s>> target=<<%s>> allow=%d\n",
                (int)stillConnected,
                Misc::getIPAddress(args.ifname).c_str(),
                Misc::getGatewayAddress(args.ifname).c_str(),
                freshSsid.c_str(), target.ssid.c_str(), (int)allow);
        if (!allow) {
            Logger::log(LogLevel::WARNING,
                        "Write-back gated: connected=%d liveSSID='%s' target='%s' (exit 5).",
                        (int)stillConnected, freshSsid.c_str(), target.ssid.c_str());
            fprintf(stderr, "[DEBUG] write-back GATED -> exit 5 (no MCU write)\n");
            return EXIT_WRITE_GATED;
        }
        auto mcu = MCU::getInstance();
        bool okId = mcu->writeUPID(target.ssid);
        bool okPw = mcu->writeUPWD(target.password);
        fprintf(stderr, "[DEBUG] mcu-write: writeUPID=%d writeUPWD=%d\n",
                (int)okId, (int)okPw);
        if (!okId || !okPw) {
            // writeUPID/writeUPWD return false on empty input or I2C failure;
            // treat as a recoverable write issue (still gate-consistent: we did
            // not corrupt a disconnected state). Surface as exit 5.
            Logger::log(LogLevel::ERROR,
                        "MCU write-back failed (okId=%d okPw=%d) (exit 5).",
                        (int)okId, (int)okPw);
            fprintf(stderr, "[DEBUG] MCU write FAILED -> exit 5\n");
            return EXIT_WRITE_GATED;
        }
        // Settle + readback-verify. The MCU's commit to non-volatile storage can
        // need a brief window after the I2C write: htc_mcu_api_test persists
        // because it keeps doing I/O after writing, whereas a process that
        // writes and exits immediately may not give the MCU time to commit. Wait
        // a little, then read back (with retry — IIC reads are single-shot) to
        // confirm the value stuck before declaring success.
        usleep(200 * 1000); // 200ms settle for MCU commit
        std::string vUpid = readMcuStrWithRetry([&mcu]{ return mcu->readUPID(); }, "UPID(verify)");
        std::string vUpwd = readMcuStrWithRetry([&mcu]{ return mcu->readUPWD(); }, "UPWD(verify)");
        fprintf(stderr, "[DEBUG] mcu-verify: readback UPID=<<%s>> UPWD=<<%s>>\n",
                vUpid.c_str(), vUpwd.c_str());
        if (vUpid != target.ssid || vUpwd != target.password) {
            Logger::log(LogLevel::ERROR,
                        "MCU write-back readback MISMATCH: UPID got='%s' want='%s', "
                        "UPWD got='%s' want='%s' (exit 5).",
                        vUpid.c_str(), target.ssid.c_str(),
                        vUpwd.c_str(), target.password.c_str());
            fprintf(stderr, "[DEBUG] MCU readback MISMATCH -> exit 5\n");
            return EXIT_WRITE_GATED;
        }
        Logger::log(LogLevel::INFO, "MCU write-back OK (UPID/UPWD persisted + verified).");
        fprintf(stderr, "[DEBUG] MCU write-back OK (verified)\n");
    }

    Logger::log(LogLevel::INFO, "htc_wifi_app done (exit 0).");
    return EXIT_OK;
}
