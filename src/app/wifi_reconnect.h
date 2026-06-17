#ifndef WIFI_RECONNECT_H
#define WIFI_RECONNECT_H

#include <string>

// Graceful WiFi SSID switch primitives.
//
// These talk to the LONG-RUNNING wpa_supplicant via its existing control
// interface socket (-p /tmp/wpa_supplicant). They NEVER kill / respawn
// wpa_supplicant, NEVER delete /tmp/wpa_supplicant, and NEVER rmmod the driver.
// This is load-bearing: T5 (src/app/main_app.cpp) deliberately keeps
// wpa_supplicant resident to avoid the DbusProcess / ctrl_iface oops. Reusing
// Misc::connectWifi to switch SSIDs would respawn supplicant (its RTL branch
// shells out to wpa_conn) and regress that fix, so we provide a non-destructive
// path here.
//
// References (behaviour mirror, not modified):
//   - src/platform/tool/wpa_conn.cpp:155  supplicant started with -C /tmp/wpa_supplicant
//   - src/platform/tool/wpa_conn.cpp:172,201  wpa_cli -i <if> -p /tmp/wpa_supplicant status
//   - src/platform/tool/wpa_conn.cpp:103-115  wpa_passphrase idiom
//
// SIM behaviour: currentSSID() returns "" and reconnectSSID() returns false
// (no wpa_supplicant / iwgetid / I2C data on the PC), so the sim path degrades
// safely without segfaulting.
namespace wifi_reconnect {

// Returns the SSID the interface is currently associated with, or "" if not
// associated / unavailable. Read-only, side-effect free.
// Prefers `iwgetid -r <if>` (busybox, pure output); falls back to
// `wpa_cli -i <if> -p /tmp/wpa_supplicant status` parsing ^ssid=.
std::string currentSSID(const std::string &ifname = "wlan0");

// Gracefully switch the long-running wpa_supplicant to <ssid>/<password> WITHOUT
// restarting the process. Sequence (all via the existing ctrl_iface socket):
//   1. wpa_passphrase -> temp conf
//   2. wpa_cli ... reconfigure  (resident supplicant re-reads conf)
//      fallback: add_network / set_network ssid,psk / enable_network / select_network
//   3. poll wpa_cli ... status for wpa_state=COMPLETED up to a timeout
// Returns true iff the link ends up associated AND isWifiConnected() is true.
// Does NOT touch wpa_supplicant lifetime. Returns false on sim.
bool reconnectSSID(const std::string &ifname,
                   const std::string &ssid,
                   const std::string &password,
                   int timeout_s = 15);

} // namespace wifi_reconnect

#endif // WIFI_RECONNECT_H
