#include "net_app_logic.h"

#include <algorithm>
#include <string>

namespace net_app_logic {

Decision decide(const LinkState &state, const Target &target)
{
    // No usable target SSID -> caller cannot connect to anything meaningful.
    if (!target.hasCredentials || target.ssid.empty()) {
        return ABORT;
    }

    if (!state.connected) {
        return FRESH_CONNECT;
    }

    // Connected: compare live SSID to the target. Same SSID -> reuse; different
    // (or none reported) -> graceful switch.
    if (normalizeSsid(state.currentSsid) == normalizeSsid(target.ssid)
        && !state.currentSsid.empty()) {
        return REUSE;
    }
    return RECONNECT;
}

int decisionExitCode(Decision d)
{
    switch (d) {
        case ABORT:          return EXIT_ARG_ERROR;       // 6
        case FRESH_CONNECT:  return EXIT_CONNECT_FAIL;    // 3 baseline; 0 on success
        case RECONNECT:      return EXIT_CONNECT_FAIL;    // 3 baseline; 0 on success
        case REUSE:          return EXIT_OK;              // 0
    }
    return EXIT_ARG_ERROR;
}

bool mayWriteBack(bool connected, const std::string &liveSsid, const std::string &targetSsid)
{
    if (!connected) {
        return false;
    }
    if (liveSsid.empty() || targetSsid.empty()) {
        return false;
    }
    return normalizeSsid(liveSsid) == normalizeSsid(targetSsid);
}

std::string normalizeSsid(const std::string &s)
{
    // SSIDs are case-sensitive per 802.11. We only trim trailing CR/LF/space
    // that shells (iwgetid / wpa_cli status) tend to append; we deliberately do
    // NOT lowercase or strip interior characters.
    std::string out = s;
    while (!out.empty()) {
        char c = out.back();
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
            out.pop_back();
        } else {
            break;
        }
    }
    // Also trim leading whitespace (defensive; iwgetid -r does not prepend it).
    std::string::size_type start = out.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return "";
    }
    if (start != 0) {
        out.erase(0, start);
    }
    return out;
}

// ============================================================================
// T7 uplink-type decision helpers. Pure: no syscalls, no Misc / Common.h dep
// (the PType integers are inlined as literals with a pointer to Common.h so the
// unit test links only this TU and stays dependency-free).
// ============================================================================

NetType parseNetType(const std::string &s)
{
    // Case-sensitive, mirroring the SSID comparison style in this module.
    if (s == "wifi") {
        return NET_WIFI;
    }
    if (s == "eth") {
        return NET_ETH;
    }
    if (s == "usb") {
        return NET_USB;
    }
    return NET_INVALID;
}

NetType ptypeToNetType(int ptype)
{
    // Values come from Common.h PTYPE_WIFI=1 / PTYPE_USB_DONGLE=4 /
    // PTYPE_ETHERNET=8. Inlined as literals so net_app_logic.cpp does not need
    // to include Common.h (keeps test_net_app_logic dependency-free).
    switch (ptype) {
        case 1: return NET_WIFI;
        case 4: return NET_USB;
        case 8: return NET_ETH;
        default: return NET_INVALID;
    }
}

std::string netTypeIfname(NetType t)
{
    // Same source as app.h WIFI_IFNAME="wlan0" / ETH_IFNAME="eth0" /
    // USB_DONGLE_IFNAME="usb0".
    switch (t) {
        case NET_WIFI: return "wlan0";
        case NET_ETH:  return "eth0";
        case NET_USB:  return "usb0";
        default:       return std::string();
    }
}

bool isNetworkUp(const std::string &ip, const std::string &gateway)
{
    // Mirrors Misc::isWifiConnected: IP non-empty AND gateway non-empty.
    // Independent of uplink type, so Eth/USB reuse it as the "link usable" test.
    return !ip.empty() && !gateway.empty();
}

bool ethNeedsConnect()
{
    // main_app current behaviour: PTYPE_ETHERNET only does setNetworkInterfaceName
    // + startDHCP. There is no connect / ifconfig-up / static-IP step.
    return false;
}

bool usbNeedsStartDefault()
{
    // main_app current behaviour: only loadDriver -> open -> preconfig. It never
    // calls UsbDongle::start(), so the 4G data context is never activated (risk
    // T7-usb-no-start). --usb-bringup flips this at the execution layer; this
    // function locks the "main_app baseline" default = false.
    return false;
}

} // namespace net_app_logic
