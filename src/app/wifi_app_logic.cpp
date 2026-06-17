#include "wifi_app_logic.h"

#include <algorithm>
#include <string>

namespace wifi_app_logic {

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

} // namespace wifi_app_logic
