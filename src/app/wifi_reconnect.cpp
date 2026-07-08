#include "wifi_reconnect.h"

#include "Misc.h"
#include "Logger.h"

#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace wifi_reconnect {

namespace {

// Fixed control-interface path used everywhere in the codebase
// (wpa_conn.cpp starts supplicant with -C /tmp/wpa_supplicant and probes with
// -p /tmp/wpa_supplicant). Reusing the SAME socket means we add no new conflict
// source for the T5 ctrl_iface/oops fix.
const char *kCtrlIfacePath = "/tmp/wpa_supplicant";
const char *kWpaCli        = "/system/bin/wifi/wpa_cli";

// Run a command and capture its stdout into <out>. Uses Misc::popencall (the
// shared system_call daemon helper). Returns true on success and fills <out>.
// On failure / empty output returns false (out is left empty).
bool capture(const std::string &cmd, std::string &out, int timeout_ms = 3000)
{
    out.clear();
    char buffer[1024] = {0};
    std::string mutableCmd = cmd; // popencall takes char*
    int rc = Misc::popencall(&mutableCmd[0], buffer, sizeof(buffer) - 1, timeout_ms);
    if (rc < 0) {
        return false;
    }
    out.assign(buffer);
    return true;
}

void runIgnore(const std::string &cmd, int timeout_ms = 5000)
{
    std::string mutableCmd = cmd;
    Misc::syscall(mutableCmd.c_str(), timeout_ms);
}

std::string trim(const std::string &s)
{
    std::string out = s;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'
                            || out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return out;
}

// Parse wpa_cli status output for ^ssid=<value> (first occurrence, line-scoped).
std::string parseSsidFromStatus(const std::string &status)
{
    std::string key = "ssid=";
    std::string::size_type pos = status.find(key);
    // wpa_cli status prints `ssid=FooBar` (or `SSID=...` depending on version);
    // we scan for the lower-case form which is what wpa_supplicant emits.
    while (pos != std::string::npos) {
        // Must be at start of line (or start of buffer).
        if (pos == 0 || status[pos - 1] == '\n') {
            std::string::size_type end = status.find('\n', pos);
            if (end == std::string::npos) {
                end = status.size();
            }
            std::string val = status.substr(pos + key.size(), end - (pos + key.size()));
            val = trim(val);
            return val;
        }
        pos = status.find(key, pos + 1);
    }
    return "";
}

bool wpaStateCompleted(const std::string &status)
{
    return status.find("wpa_state=COMPLETED") != std::string::npos;
}

} // namespace

std::string currentSSID(const std::string &ifname)
{
#ifdef BUILD_FOR_SIMULATION
    // No wpa_supplicant / iwgetid / live association on PC. Return empty so the
    // decision layer degrades safely (no segfault, no false SSID).
    Logger::log(LogLevel::INFO, "[SIM] currentSSID(%s): stub returns empty", ifname.c_str());
    return "";
#else
    if (ifname.empty()) {
        return "";
    }

    // 1) Prefer iwgetid -r (busybox, pure output, no side effects).
    {
        std::string out;
        std::string cmd = "iwgetid -r " + ifname;
        if (capture(cmd, out, 2000)) {
            out = trim(out);
            if (!out.empty()) {
                return out;
            }
        }
    }

    // 2) Fallback: query the resident supplicant's status via ctrl_iface.
    {
        std::string out;
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s -i %s -p %s status 2>/dev/null",
                 kWpaCli, ifname.c_str(), kCtrlIfacePath);
        if (capture(cmd, out, 3000)) {
            std::string ssid = parseSsidFromStatus(out);
            if (!ssid.empty()) {
                return ssid;
            }
        }
    }

    return "";
#endif
}

bool reconnectSSID(const std::string &ifname,
                   const std::string &ssid,
                   const std::string &password,
                   int timeout_s)
{
#ifdef BUILD_FOR_SIMULATION
    (void)ifname;
    (void)ssid;
    (void)password;
    (void)timeout_s;
    Logger::log(LogLevel::INFO, "[SIM] reconnectSSID: stub returns false (no wpa_supplicant)");
    return false;
#else
    if (ifname.empty() || ssid.empty()) {
        return false;
    }
    if (timeout_s <= 0) {
        timeout_s = 15;
    }

    Logger::log(LogLevel::INFO,
                "Graceful WiFi reconnect to %s on %s (no process restart)",
                ssid.c_str(), ifname.c_str());

    // 1) Switch the RESIDENT supplicant to the target SSID in place, WITHOUT
    //    restarting it (preserves the T5 ctrl_iface/oops fix). RECONNECT is only
    //    entered when already connected, so network id 0 already exists
    //    (connectWifi/wpa_conn created it as the sole network block); overwrite
    //    it and re-select.
    //
    //    We deliberately do NOT use `wpa_cli reconfigure`: on this device it
    //    (a) reloads the supplicant's ORIGINAL -c config (/config/profiles/
    //    wpa_supplicant.conf, which still holds the OLD SSID — we never wrote
    //    there), so it re-asserts the old SSID; and (b) blocks long enough to
    //    trip the system_call daemon's wait timeout, then completes ASYNC and
    //    races/reverts our in-memory set_network edits (observed on T32: the
    //    reconfigure timeout fires mid-sequence and the link snaps back to the
    //    old SSID, and it leaves the system_call daemon unhealthy for the next
    //    currentSSID() read). set_network + select_network return immediately
    //    and are the standard no-restart SSID switch.
    {
        char setSsid[512];
        snprintf(setSsid, sizeof(setSsid),
                 "%s -i %s -p %s set_network 0 ssid '\"%s\"' 2>/dev/null",
                 kWpaCli, ifname.c_str(), kCtrlIfacePath, ssid.c_str());
        runIgnore(setSsid, 3000);

        char setPsk[512];
        snprintf(setPsk, sizeof(setPsk),
                 "%s -i %s -p %s set_network 0 psk '\"%s\"' 2>/dev/null",
                 kWpaCli, ifname.c_str(), kCtrlIfacePath, password.c_str());
        runIgnore(setPsk, 3000);

        char enable[256];
        snprintf(enable, sizeof(enable),
                 "%s -i %s -p %s enable_network 0 2>/dev/null",
                 kWpaCli, ifname.c_str(), kCtrlIfacePath);
        runIgnore(enable, 3000);

        // select_network 0 disables all other networks and connects to id 0 ->
        // triggers a fresh scan+associate to the target SSID.
        char select[256];
        snprintf(select, sizeof(select),
                 "%s -i %s -p %s select_network 0 2>/dev/null",
                 kWpaCli, ifname.c_str(), kCtrlIfacePath);
        runIgnore(select, 3000);
    }

    // 3) Settle + poll for L2 association to the target SSID. select_network
    //    triggers a scan + re-associate that takes a few seconds on this device
    //    (T32: ~3-5s to reach COMPLETED on the new SSID). Judging on the FIRST
    //    status (taken immediately after select) is a RACE: wpa_supplicant can
    //    transiently report the SELECTED network's SSID while still mid-handshake
    //    -> false "matched" on poll iteration 0 -> the caller's post-probe then
    //    sees the not-yet-settled link and bails (observed on T32: exit 3, no
    //    DHCP ever runs). So (a) give it a short settle window before the first
    //    judge, then (b) require wpa_state=COMPLETED AND ssid==target on TWO
    //    consecutive checks 1s apart (debounce) before declaring success.
    //    Judge by L2 (NOT Misc::isWifiConnected / L3 IP): this function does NOT
    //    run DHCP; the caller (wifi_app main) runs DHCP after we return true.
    std::this_thread::sleep_for(std::chrono::seconds(2));  // settle window (no fork)

    int consecutive = 0;
    for (int waited = 0; waited < timeout_s; ++waited) {
        std::string status;
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "%s -i %s -p %s status 2>/dev/null",
                 kWpaCli, ifname.c_str(), kCtrlIfacePath);
        bool ok = capture(cmd, status, 3000);
        std::string parsedSsid = ok ? parseSsidFromStatus(status) : std::string("<capture-failed>");
        fprintf(stderr, "[DEBUG] poll[%d]: capture=%d completed=%d parsed=<<%s>> want=<<%s>>\n",
                waited, (int)ok, (int)(ok && wpaStateCompleted(status)),
                parsedSsid.c_str(), ssid.c_str());
        if (ok && wpaStateCompleted(status)
            && parseSsidFromStatus(status) == ssid) {
            if (++consecutive >= 2) {
                return true;
            }
        } else {
            consecutive = 0;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Final judge: L2 association to the target SSID (re-query once).
    return currentSSID(ifname) == ssid;
#endif
}

} // namespace wifi_reconnect
