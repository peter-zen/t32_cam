#ifndef NET_APP_LOGIC_H
#define NET_APP_LOGIC_H

#include <string>

// Pure-logic layer for net.
//
// This header/translation unit contains NO system calls. It holds:
//   - the WiFi-specific "should we reconnect / should we write back / what exit
//     code" decision logic (behaviour-equivalent to the former htc_wifi_app);
//   - the T7 uplink-type decision helpers (NetType parsing, INI PType mapping,
//     per-uplink interface name, isNetworkUp predicate).
// Everything is unit-testable on the PC simulation build without a real WiFi
// stack, MCU bus, wpa_supplicant, Ethernet link or USB dongle.
//
// Exit code contract (stable, consumed by scripts/daemon callers):
//   0  success
//   2  driver load failure
//   3  connection failure (incl. graceful reconnect failure)
//   4  DHCP failure
//   5  connected OK but MCU write-back was gated/skipped (recoverable, non-0)
//   6  argument / credential error
namespace net_app_logic {

// Stable exit codes (mirror the planner §4 contract).
enum ExitCode {
    EXIT_OK              = 0,
    EXIT_DRIVER_FAIL     = 2,
    EXIT_CONNECT_FAIL    = 3,
    EXIT_DHCP_FAIL       = 4,
    EXIT_WRITE_GATED     = 5,
    EXIT_ARG_ERROR       = 6,
};

// What the orchestrator should do next, given the current link state and the
// connection target. Pure function of (connected, current_ssid, target_ssid,
// has_credentials).
enum Decision {
    ABORT,          // no usable credentials (empty target SSID) -> exit 6
    FRESH_CONNECT,  // not connected at all -> Misc::connectWifi (first connect)
    REUSE,          // already connected to the target SSID -> skip, straight to DHCP
    RECONNECT,      // connected but to a different SSID -> graceful switch
};

struct LinkState {
    bool connected;          // isWifiConnected()
    std::string currentSsid; // currentSSID() ("" on sim / not associated)
};

struct Target {
    std::string ssid;
    std::string password;
    bool        hasCredentials; // target.ssid non-empty (password may legitimately be empty for open nets)
};

// Decide the next action. Pure: no syscalls, no global state.
Decision decide(const LinkState &state, const Target &target);

// Map a Decision to its baseline exit code (before DHCP/write-back refinements).
int decisionExitCode(Decision d);

// Strict gate for MCU write-back: the caller may ONLY write UPID/UPWD when this
// returns true. Requires BOTH connected==true AND the live SSID equals target.
// Callers must re-read currentSSID() immediately before writing and pass the
// freshly-read value here (defends against "connected but landed on another SSID").
bool mayWriteBack(bool connected, const std::string &liveSsid, const std::string &targetSsid);

// Normalize an SSID for comparison (trim trailing whitespace/newlines; SSIDs are
// case-sensitive per 802.11, so we deliberately do NOT lowercase).
std::string normalizeSsid(const std::string &s);

// ============================================================================
// T7 uplink-type decision helpers (no syscalls, PC-unit-testable).
// Added alongside the Ethernet + USB dongle uplinks so the "which uplink /
// which interface / is the link usable" decisions live in pure logic rather
// than buried in the syscall-heavy main.
// ============================================================================

// Uplink category. NET_INVALID is returned by the parsers for anything that
// does not map to a supported uplink (bad CLI string, unknown INI PType).
enum NetType {
    NET_WIFI,     // PTYPE_WIFI       (Common.h = 1), ifname wlan0
    NET_USB,      // PTYPE_USB_DONGLE (Common.h = 4), ifname usb0
    NET_ETH,      // PTYPE_ETHERNET   (Common.h = 8), ifname eth0
    NET_INVALID,
};

// CLI "--type" string -> NetType. Case-sensitive ("wifi"/"eth"/"usb" only);
// any other token (including "WIFI"/"WiFi"/""/"bogus") returns NET_INVALID.
NetType parseNetType(const std::string &s);

// INI BOOT/PType integer -> NetType. Locks Common.h PTYPE_WIFI=1 /
// PTYPE_USB_DONGLE=4 / PTYPE_ETHERNET=8. Anything else (0/2/3/9/...) is
// NET_INVALID.
NetType ptypeToNetType(int ptype);

// NetType -> interface name (same source as app.h WIFI_IFNAME/ETH_IFNAME/
// USB_DONGLE_IFNAME). NET_INVALID -> "".
std::string netTypeIfname(NetType t);

// "Network usable" predicate, identical in semantics to Misc::isWifiConnected
// (IP non-empty AND gateway non-empty -- independent of uplink type). Pure:
// caller passes the ip/gateway strings obtained from Misc::getIPAddress /
// Misc::getGatewayAddress at the execution layer.
bool isNetworkUp(const std::string &ip, const std::string &gateway);

// Does Ethernet need an explicit connect step? main_app current behaviour = NO
// (PTYPE_ETHERNET only does setNetworkInterfaceName("eth0") + startDHCP("eth0"),
// no ifconfig-up / static IP / connect). Recorded as a constant so the
// semantics is locked by a unit test.
bool ethNeedsConnect();

// Does the USB dongle path call UsbDongle::start() by default? main_app current
// behaviour = NO (only loadDriver -> open -> preconfig, context never activated
// -- a pre-existing main_app limitation, surfaced as risk T7-usb-no-start).
// --usb-bringup flips this at the execution layer; this function returns the
// "main_app baseline" = false so a unit test locks the default.
bool usbNeedsStartDefault();

} // namespace net_app_logic

#endif // NET_APP_LOGIC_H
