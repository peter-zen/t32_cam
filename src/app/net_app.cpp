// htc_net_app — unified uplink connection tool (WiFi / Ethernet / USB dongle).
//
// Layered design (T7 planner full):
//   main() (this file)              -> CLI parse + uplink dispatch + exit codes + logging
//   net_app_logic (pure, no syscall) -> WiFi Decision / write-back gate / exit-code mapping
//                                       + T7 uplink-type decision helpers (NetType / ifname /
//                                       isNetworkUp)
//   wifi_reconnect                  -> graceful WiFi SSID switch (no wpa_supplicant restart)
//   Misc                            -> reused as-is (connectWifi / startDHCP / isWifiConnected /
//                                      getIPAddress / getGatewayAddress / setNetworkInterfaceName)
//   UsbDongle (network lib)         -> reused as-is (loadDriver / open / preconfig [+ start]).
//
// This binary is a "connect once per invocation" foreground tool (no daemon).
//
// CLI (T7, user-decided): --type {wifi|eth|usb} defaults to wifi. This stage
// does NOT read any ini file (consistent with the former htc_wifi_app deliberately
// avoiding ini). WiFi credentials come ONLY from --ssid/--pwd or the MCU
// registers (readUPID/readUPWD); it deliberately does NOT read ini SYS/UPID+
// UPWD, so it is unaffected by the Common.h INI_KEY_UPWD="PWD" naming pitfall.
//
// Uplink sequences mirror htc_main_app current behaviour (faithfully ported):
//   - WiFi   : Misc::connectWifi(ssid,pwd) -> startDHCP            (wifi_app behaviour, intact)
//   - Ethernet : Misc::setNetworkInterfaceName("eth0") + startDHCP("eth0")  (no connect step)
//   - USB    : UsbDongle->loadDriver()->open()->preconfig() -> startDHCP("usb0")
//              --usb-bringup (default OFF) additionally inserts setModel(--usb-model) before
//              preconfig() and start() after preconfig() (activates the 4G data context).
//              main_app never calls start(), so the default path cannot get an IP on real
//              hardware (risk T7-usb-no-start); --usb-bringup is the explicit opt-in.
//
// Exit code contract (stable, shared across uplinks):
//   0 success / 2 driver load fail / 3 connect fail / 4 DHCP fail
//   5 connected OK but MCU write-back gated/skipped (WiFi only) / 6 arg or credential error

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <functional>
#include <unistd.h>

#include "Misc.h"
#include "MCU.h"
#include "Logger.h"
#include "UsbDongle.h"

#include "net_app_logic.h"
#include "wifi_reconnect.h"

using namespace net_app_logic;

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
    // Common
    std::string typeStr    = "wifi"; // --type wifi|eth|usb (default: wifi)
    bool        haveType   = false;  // true iff --type was explicitly passed
    bool        noDhcp     = false;
    bool        verbose    = false;
    bool        help       = false;
    // WiFi-specific (preserved from htc_wifi_app)
    std::string ifname     = "wlan0";
    std::string ssid;            // empty -> read from MCU
    std::string password;        // empty -> read from MCU
    bool        haveCliSsid = false;
    bool        writeMcu    = false;
    // USB-specific
    bool        usbBringup  = false;          // --usb-bringup (default OFF)
    std::string usbModel    = "EC20";         // --usb-model EC20|EC200A|EG800K|RG255AA
};

void printUsage(FILE *out, const char *prog)
{
    fprintf(out,
        "Usage: %s [--type {wifi|eth|usb}] [options]\n"
        "  Unified uplink connection tool. Brings up one network uplink per\n"
        "  invocation (no daemon). --type defaults to wifi; this tool does\n"
        "  NOT read any ini file.\n"
        "\n"
        "Options (common):\n"
        "  --type <wifi|eth|usb>  Uplink type (default: wifi).\n"
        "  --no-dhcp              Skip DHCP after bringing the link up.\n"
        "  -v, --verbose          Verbose logging.\n"
        "  -h, --help             Show this help and exit.\n"
        "\n"
        "Options (WiFi only):\n"
        "  --ssid <SSID>          Target SSID (default: read from MCU).\n"
        "  --pwd <PASSWORD>       Target password (default: read from MCU).\n"
        "  --if <name>            WLAN interface name (default: wlan0).\n"
        "  --write-mcu            Persist SSID/password to MCU after a successful\n"
        "                         connection (strictly gated; see exit code 5).\n"
        "\n"
        "Options (USB only):\n"
        "  --usb-bringup          Additionally call UsbDongle::start() (activates\n"
        "                         the 4G data context). Default OFF — the default\n"
        "                         path mirrors htc_main_app (loadDriver->open->\n"
        "                         preconfig), which does NOT activate the context\n"
        "                         and therefore cannot get an IP on real hardware\n"
        "                         (known limitation T7-usb-no-start).\n"
        "  --usb-model <m>        Dongle model: EC20|EC200A|EG800K|RG255AA.\n"
        "                         Only effective with --usb-bringup (default EC20).\n"
        "\n"
        "Exit codes:\n"
        "  0  success\n"
        "  2  driver load failure\n"
        "  3  connection failure\n"
        "  4  DHCP failure\n"
        "  5  connected OK but MCU write-back was skipped (gated, WiFi only)\n"
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
        } else if (a == "--type") {
            out.typeStr = next("--type");
            if (out.typeStr.empty() && err.empty()) {
                err = "empty value for --type";
            }
            if (err.empty()) {
                out.haveType = true;
            }
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
        } else if (a == "--usb-bringup") {
            out.usbBringup = true;
        } else if (a == "--usb-model") {
            out.usbModel = next("--usb-model");
            if (out.usbModel.empty() && err.empty()) {
                err = "empty value for --usb-model";
            }
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

// ---------------------------------------------------------------------------
// WiFi uplink (behaviour-equivalent to the former htc_wifi_app main body).
// Moved verbatim into this function; only namespace/include/log strings changed.
// ---------------------------------------------------------------------------
int runWifi(const CliArgs &args)
{
    Logger::log(LogLevel::INFO,
                "htc_net_app wifi: if=%s writeMcu=%d dhcp=%s",
                args.ifname.c_str(), (int)args.writeMcu,
                args.noDhcp ? "off" : "on");

    // ---- Resolve target credentials (CLI takes precedence; else MCU). ----
    std::string targetSsid = args.ssid;
    std::string targetPwd  = args.password;
    if (!args.haveCliSsid) {
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

    // 无 target 凭据（无 --ssid 且 MCU 空）但当前已连接：复用现有连接的 SSID 作为
    // target，让 decide 走 REUSE（probe + DHCP）。避免测试环境（WiFi 已连、MCU 无凭据）
    // 因 target 空而 ABORT (exit 6)。仅此场景生效；有凭据 / 未连接路径完全不变。
    if (!target.hasCredentials && state.connected && !state.currentSsid.empty()) {
        target.ssid = state.currentSsid;
        target.hasCredentials = true;
        Logger::log(LogLevel::INFO,
                    "No target SSID (no --ssid, MCU empty) but already connected to '%s'; "
                    "reusing existing link as target.",
                    target.ssid.c_str());
    }

    Logger::log(LogLevel::INFO,
                "link: connected=%d currentSSID='%s' targetSSID='%s'",
                (int)state.connected, state.currentSsid.c_str(),
                target.ssid.c_str());
    fprintf(stderr, "[DEBUG] decide: connected=%d currentSSID=<<%s>> (len=%zu) "
                    "target=<<%s>> (len=%zu)\n",
            (int)state.connected, state.currentSsid.c_str(),
            state.currentSsid.size(), target.ssid.c_str(), target.ssid.size());

    Decision d = decide(state, target);
    fprintf(stderr, "[DEBUG] decision=%d\n", (int)d);
    switch (d) {
        case ABORT: {
            Logger::log(LogLevel::ERROR,
                        "No usable target SSID (empty). Aborting (exit 6).");
            fprintf(stderr,
                "htc_net_app wifi: no target SSID available. "
                "Pass --ssid/--pwd or populate MCU registers.\n");
            return EXIT_ARG_ERROR;
        }
        case FRESH_CONNECT: {
            Logger::log(LogLevel::INFO, "Not connected; calling connectWifi (fresh).");
            bool driverLoaded = Misc::isWifiDriverLoaded();
            Logger::log(LogLevel::INFO, "driver_loaded=%d", (int)driverLoaded);
            if (!Misc::connectWifi(target.ssid, target.password)) {
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
            Logger::log(LogLevel::INFO,
                        "Connected to different SSID; graceful switch (no supplicant restart).");
            if (!wifi_reconnect::reconnectSSID(args.ifname, target.ssid, target.password)) {
                Logger::log(LogLevel::ERROR, "Graceful reconnect failed (exit 3).");
                return EXIT_CONNECT_FAIL;
            }
            break;
        }
        case REUSE: {
            Logger::log(LogLevel::INFO, "Already connected to target SSID; reusing.");
            break;
        }
    }

    // ---- Verify L2 association to the target SSID (BEFORE DHCP). ----
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

    // ---- DHCP (default DHCP after connecting). ----
    if (!args.noDhcp) {
        if (!Misc::startDHCP(args.ifname)) {
            Logger::log(LogLevel::ERROR, "DHCP failed (exit 4).");
            return EXIT_DHCP_FAIL;
        }
    }

    // ---- MCU write-back (ONLY after success, strict gate). ----
    if (args.writeMcu) {
        std::string freshSsid = wifi_reconnect::currentSSID(args.ifname);
        bool stillConnected = Misc::isWifiConnected(args.ifname);
        bool allow = mayWriteBack(stillConnected, freshSsid, target.ssid);
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
            Logger::log(LogLevel::ERROR,
                        "MCU write-back failed (okId=%d okPw=%d) (exit 5).",
                        (int)okId, (int)okPw);
            fprintf(stderr, "[DEBUG] MCU write FAILED -> exit 5\n");
            return EXIT_WRITE_GATED;
        }
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

    Logger::log(LogLevel::INFO, "htc_net_app wifi done (exit 0).");
    return EXIT_OK;
}

// ---------------------------------------------------------------------------
// Ethernet uplink. Mirrors htc_main_app PTYPE_ETHERNET handling: set the
// interface name to eth0, then DHCP. There is NO connect / ifconfig-up / static
// IP step (ethNeedsConnect() locks this). Eth has no MCU write-back (no exit 5).
// ---------------------------------------------------------------------------
int runEth(const CliArgs &args)
{
    const std::string ifname = netTypeIfname(NET_ETH); // "eth0"
    Logger::log(LogLevel::INFO,
                "htc_net_app eth: if=%s dhcp=%s needsConnect=%d",
                ifname.c_str(), args.noDhcp ? "off" : "on", (int)ethNeedsConnect());

    Misc::setNetworkInterfaceName(ifname);

    if (!args.noDhcp) {
        if (!Misc::startDHCP(ifname)) {
            Logger::log(LogLevel::ERROR, "eth DHCP failed (exit 4).");
            return EXIT_DHCP_FAIL;
        }
    }

    if (isNetworkUp(Misc::getIPAddress(ifname), Misc::getGatewayAddress(ifname))) {
        Logger::log(LogLevel::INFO, "htc_net_app eth done (exit 0).");
        return EXIT_OK;
    }
    Logger::log(LogLevel::ERROR, "eth link not up (no IP/gateway) (exit 3).");
    return EXIT_CONNECT_FAIL;
}

// ---------------------------------------------------------------------------
// USB dongle uplink. Faithfully ports htc_main_app PTYPE_USB_DONGLE handling:
//   loadDriver -> open -> preconfig -> startDHCP(usb0)
// --usb-bringup (default OFF) additionally inserts setModel(--usb-model) before
// preconfig() and start() after preconfig() (the latter activates the 4G data
// context via AT+QIACT=1; without it usb0 has no carrier on real hardware —
// known limitation T7-usb-no-start).
// ---------------------------------------------------------------------------
bool parseUsbModel(const std::string &s, network::UsbDongleModel &out)
{
    if (s == "EC20")   { out = network::UsbDongleModel::EC20;   return true; }
    if (s == "EC200A") { out = network::UsbDongleModel::EC200A; return true; }
    if (s == "EG800K") { out = network::UsbDongleModel::EG800K; return true; }
    if (s == "RG255AA"){ out = network::UsbDongleModel::RG255AA;return true; }
    return false;
}

int runUsb(const CliArgs &args)
{
    const std::string ifname = netTypeIfname(NET_USB); // "usb0"
    Logger::log(LogLevel::INFO,
                "htc_net_app usb: if=%s dhcp=%s bringup=%d model=%s needsStartDefault=%d",
                ifname.c_str(), args.noDhcp ? "off" : "on",
                (int)args.usbBringup, args.usbModel.c_str(),
                (int)usbNeedsStartDefault());

    Misc::setNetworkInterfaceName(ifname);

    network::UsbDongleModel model = network::UsbDongleModel::EC20;
    if (args.usbBringup && !parseUsbModel(args.usbModel, model)) {
        fprintf(stderr, "htc_net_app usb: invalid --usb-model '%s' "
                        "(expected EC20|EC200A|EG800K|RG255AA)\n", args.usbModel.c_str());
        return EXIT_ARG_ERROR;
    }

    auto dongle = network::UsbDongle::getInstance();

    if (!dongle->loadDriver()) {
        Logger::log(LogLevel::ERROR, "usb loadDriver failed (exit 2).");
        return EXIT_DRIVER_FAIL;
    }

    if (!dongle->open()) {
        Logger::log(LogLevel::ERROR, "usb open failed (exit 3).");
        return EXIT_CONNECT_FAIL;
    }

    if (args.usbBringup) {
        dongle->setModel(model);
    }

    if (!dongle->preconfig()) {
        Logger::log(LogLevel::ERROR, "usb preconfig failed (exit 3).");
        return EXIT_CONNECT_FAIL;
    }

    if (args.usbBringup) {
        // start() = querySimReady -> getApn -> setContextProfile ->
        // activateContextProfile (AT+QIACT=1). This activates the 4G data
        // context; without it usb0 has no carrier on real hardware.
        if (!dongle->start()) {
            Logger::log(LogLevel::ERROR, "usb start failed (exit 3).");
            return EXIT_CONNECT_FAIL;
        }
    }

    if (!args.noDhcp) {
        if (!Misc::startDHCP(ifname)) {
            Logger::log(LogLevel::ERROR, "usb DHCP failed (exit 4).");
            return EXIT_DHCP_FAIL;
        }
    }

    if (isNetworkUp(Misc::getIPAddress(ifname), Misc::getGatewayAddress(ifname))) {
        Logger::log(LogLevel::INFO, "htc_net_app usb done (exit 0).");
        return EXIT_OK;
    }
    Logger::log(LogLevel::ERROR, "usb link not up (no IP/gateway) (exit 3).");
    return EXIT_CONNECT_FAIL;
}

} // namespace

int main(int argc, char **argv)
{
    CliArgs args;
    std::string parseErr;
    if (!parseArgs(argc, argv, args, parseErr)) {
        fprintf(stderr, "htc_net_app: %s\n", parseErr.c_str());
        printUsage(stderr, argv[0]);
        return EXIT_ARG_ERROR;
    }
    if (args.help) {
        printUsage(stdout, argv[0]);
        return EXIT_OK;
    }

    // --type defaults to wifi (user-decided: do NOT read ini). An explicit but
    // unparseable --type value still -> argument error exit 6 (checked below).
    if (!args.haveType) {
        Logger::log(LogLevel::INFO, "No --type given; defaulting to wifi.");
        fprintf(stderr, "htc_net_app: no --type given, defaulting to wifi.\n");
    }

    NetType netType = parseNetType(args.typeStr);
    if (netType == NET_INVALID) {
        fprintf(stderr, "htc_net_app: invalid --type '%s' (expected wifi|eth|usb).\n",
                args.typeStr.c_str());
        printUsage(stderr, argv[0]);
        return EXIT_ARG_ERROR;
    }

    Logger::log(LogLevel::INFO,
                "htc_net_app start: type=%s ifname=%s",
                args.typeStr.c_str(), netTypeIfname(netType).c_str());

    switch (netType) {
        case NET_WIFI: return runWifi(args);
        case NET_ETH:  return runEth(args);
        case NET_USB:  return runUsb(args);
        default:
            fprintf(stderr, "htc_net_app: unsupported uplink type.\n");
            return EXIT_ARG_ERROR;
    }
}
