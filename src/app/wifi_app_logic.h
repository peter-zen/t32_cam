#ifndef WIFI_APP_LOGIC_H
#define WIFI_APP_LOGIC_H

#include <string>

// Pure-logic layer for htc_wifi_app.
//
// This header/translation unit contains NO system calls. It only holds the
// "should we reconnect / should we write back / what exit code" decision logic,
// so it can be unit-tested on the PC simulation build without a real WiFi
// stack, MCU bus or wpa_supplicant.
//
// Exit code contract (stable, consumed by scripts/daemon callers):
//   0  success
//   2  driver load failure
//   3  connection failure (incl. graceful reconnect failure)
//   4  DHCP failure
//   5  connected OK but MCU write-back was gated/skipped (recoverable, non-0)
//   6  argument / credential error
namespace wifi_app_logic {

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

} // namespace wifi_app_logic

#endif // WIFI_APP_LOGIC_H
