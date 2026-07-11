// Unit tests for the pure-logic layer of net.
//
// Covers:
//   WiFi-specific (ported verbatim from the former test_wifi_app_logic):
//   - decide(): ABORT / FRESH_CONNECT / RECONNECT / REUSE
//   - decisionExitCode(): Decision -> baseline exit code mapping
//   - mayWriteBack(): strict gate (connected + live SSID == target)
//   - normalizeSsid(): trim only, case-sensitive preserved
//   T7 uplink-type helpers (new):
//   - parseNetType(): CLI "--type" string -> NetType
//   - ptypeToNetType(): INI BOOT/PType int -> NetType (locks Common.h 1/4/8)
//   - netTypeIfname(): NetType -> wlan0/eth0/usb0
//   - isNetworkUp(): IP non-empty AND gateway non-empty
//   - ethNeedsConnect(): false (ETH has no connect step)
//   - usbNeedsStartDefault(): false (main_app never calls start())
//
// This translation unit has NO system calls; it links only against
// net_app_logic. Build under BUILD_FOR_SIMULATION only.
//
// Exit: 0 = all pass, non-zero = failure.

#include "net_app_logic.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace net_app_logic;

namespace {

int g_failures = 0;

template <typename A, typename E>
void expectEqImpl(const A &a, const E &e, const char *expr, const char *msg, int line)
{
    if (!(a == e)) {
        std::fprintf(stderr, "FAIL [%s]: line %d: %s\n", msg, line, expr);
        ++g_failures;
    }
}

#define EXPECT_EQ(actual, expected, msg)                                      \
    do {                                                                      \
        auto _a = (actual);                                                   \
        auto _e = (expected);                                                 \
        expectEqImpl(_a, _e, #actual " (got) vs " #expected " (expected)",    \
                     msg, __LINE__);                                          \
    } while (0)

#define EXPECT_TRUE(x, msg)                                                   \
    do {                                                                      \
        if (!(x)) {                                                           \
            std::fprintf(stderr, "FAIL [%s]: line %d: %s is false\n",        \
                         msg, __LINE__, #x);                                  \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

#define EXPECT_FALSE(x, msg)                                                  \
    do {                                                                      \
        if ((x)) {                                                            \
            std::fprintf(stderr, "FAIL [%s]: line %d: %s is true\n",         \
                         msg, __LINE__, #x);                                  \
            ++g_failures;                                                     \
        }                                                                     \
    } while (0)

// ===== WiFi-specific (ported verbatim from test_wifi_app_logic) =====

void testDecisionReuse()
{
    LinkState s{true, "HomeNet"};
    Target t{"HomeNet", "secret", true};
    EXPECT_EQ(decide(s, t), REUSE, "reuse when connected to target");
}

void testDecisionReconnect()
{
    LinkState s{true, "OldNet"};
    Target t{"NewNet", "secret", true};
    EXPECT_EQ(decide(s, t), RECONNECT, "reconnect when connected to different ssid");
}

void testDecisionReconnectWhenConnectedButNoSsidReported()
{
    // Connected but currentSSID() returned "" (e.g. transient): must not be
    // treated as "already on target" -> RECONNECT is the safe choice.
    LinkState s{true, ""};
    Target t{"NewNet", "secret", true};
    EXPECT_EQ(decide(s, t), RECONNECT, "reconnect when connected but ssid unknown");
}

void testDecisionFreshConnect()
{
    LinkState s{false, ""};
    Target t{"AnyNet", "secret", true};
    EXPECT_EQ(decide(s, t), FRESH_CONNECT, "fresh connect when not connected");
}

void testDecisionAbortNoCredentials()
{
    LinkState s{false, ""};
    Target t{"", "", false};
    EXPECT_EQ(decide(s, t), ABORT, "abort when target ssid empty");

    // hasCredentials=false forces abort even if ssid string somehow set.
    LinkState s2{true, "X"};
    Target t2{"Y", "z", false};
    EXPECT_EQ(decide(s2, t2), ABORT, "abort when hasCredentials false");
}

void testDecisionCaseSensitiveSsid()
{
    // SSIDs are case-sensitive; "Home" != "home" -> reconnect.
    LinkState s{true, "Home"};
    Target t{"home", "secret", true};
    EXPECT_EQ(decide(s, t), RECONNECT, "ssid comparison is case-sensitive");
}

void testDecisionExitCodeMapping()
{
    EXPECT_EQ(decisionExitCode(ABORT), 6, "abort->6");
    EXPECT_EQ(decisionExitCode(FRESH_CONNECT), 3, "fresh->3 baseline");
    EXPECT_EQ(decisionExitCode(RECONNECT), 3, "reconnect->3 baseline");
    EXPECT_EQ(decisionExitCode(REUSE), 0, "reuse->0");
}

void testMayWriteBackGate()
{
    // Happy path: connected and live == target.
    EXPECT_TRUE(mayWriteBack(true, "HomeNet", "HomeNet"), "gate allows matching ssid");

    // Not connected -> never write.
    EXPECT_FALSE(mayWriteBack(false, "HomeNet", "HomeNet"), "gate blocks when not connected");

    // Connected but different SSID -> never write (the core safety property).
    EXPECT_FALSE(mayWriteBack(true, "OtherNet", "HomeNet"), "gate blocks mismatched ssid");

    // Empty live SSID -> never write (can't confirm target).
    EXPECT_FALSE(mayWriteBack(true, "", "HomeNet"), "gate blocks empty live ssid");

    // Empty target -> never write.
    EXPECT_FALSE(mayWriteBack(true, "HomeNet", ""), "gate blocks empty target");
}

void testNormalizeSsidTrimsOnly()
{
    EXPECT_EQ(normalizeSsid("HomeNet\n"), std::string("HomeNet"), "trim trailing newline");
    EXPECT_EQ(normalizeSsid("HomeNet\r\n"), std::string("HomeNet"), "trim trailing crlf");
    EXPECT_EQ(normalizeSsid("  HomeNet  "), std::string("HomeNet"), "trim surrounding spaces");
    // Interior / case preserved.
    EXPECT_EQ(normalizeSsid("My  Net"), std::string("My  Net"), "preserve interior spaces");
    EXPECT_EQ(normalizeSsid("Home"), std::string("Home"), "case preserved lowercase/uppercase");
    EXPECT_EQ(normalizeSsid("home"), std::string("home"), "case-sensitive not lowercased");
    EXPECT_EQ(normalizeSsid("   "), std::string(""), "whitespace only -> empty");
}

void testEndToEndDecisionThenGate()
{
    // Scenario: connected to OldNet, target NewNet -> RECONNECT; after a
    // successful reconnect the gate must still be checked with the live SSID.
    LinkState before{true, "OldNet"};
    Target t{"NewNet", "pw", true};
    EXPECT_EQ(decide(before, t), RECONNECT, "e2e: reconnect decision");

    // Simulate "reconnect succeeded, now on target": gate passes.
    EXPECT_TRUE(mayWriteBack(true, "NewNet", "NewNet"), "e2e: gate passes after reconnect");

    // Simulate "reconnect reported success but actually landed elsewhere":
    // gate correctly blocks the write (exit 5 path).
    EXPECT_FALSE(mayWriteBack(true, "OldNet", "NewNet"),
                 "e2e: gate blocks write when landed on wrong ssid");
}

// ===== T7 uplink-type helpers (new) =====

void testParseNetType()
{
    // Accepted tokens (case-sensitive).
    EXPECT_EQ(parseNetType("wifi"), NET_WIFI, "wifi -> NET_WIFI");
    EXPECT_EQ(parseNetType("eth"),  NET_ETH,  "eth -> NET_ETH");
    EXPECT_EQ(parseNetType("usb"),  NET_USB,  "usb -> NET_USB");

    // Everything else is invalid (case-sensitive, mirroring SSID style).
    EXPECT_EQ(parseNetType("WIFI"),   NET_INVALID, "WIFI uppercase -> invalid");
    EXPECT_EQ(parseNetType("WiFi"),   NET_INVALID, "WiFi mixed -> invalid");
    EXPECT_EQ(parseNetType("ETH"),    NET_INVALID, "ETH uppercase -> invalid");
    EXPECT_EQ(parseNetType("USB"),    NET_INVALID, "USB uppercase -> invalid");
    EXPECT_EQ(parseNetType(""),       NET_INVALID, "empty -> invalid");
    EXPECT_EQ(parseNetType("bogus"),  NET_INVALID, "bogus -> invalid");
    EXPECT_EQ(parseNetType("ethernet"),NET_INVALID,"ethernet full -> invalid");
}

void testPtypeToNetType()
{
    // Locks Common.h PTYPE_WIFI=1 / PTYPE_USB_DONGLE=4 / PTYPE_ETHERNET=8.
    EXPECT_EQ(ptypeToNetType(1), NET_WIFI, "PType 1 -> WIFI");
    EXPECT_EQ(ptypeToNetType(4), NET_USB,  "PType 4 -> USB");
    EXPECT_EQ(ptypeToNetType(8), NET_ETH,  "PType 8 -> ETH");

    // Anything else is invalid.
    EXPECT_EQ(ptypeToNetType(0), NET_INVALID, "PType 0 -> invalid");
    EXPECT_EQ(ptypeToNetType(2), NET_INVALID, "PType 2 -> invalid");
    EXPECT_EQ(ptypeToNetType(3), NET_INVALID, "PType 3 -> invalid");
    EXPECT_EQ(ptypeToNetType(9), NET_INVALID, "PType 9 -> invalid");
}

void testNetTypeIfname()
{
    // Same source as app.h WIFI_IFNAME="wlan0" / ETH_IFNAME="eth0" /
    // USB_DONGLE_IFNAME="usb0".
    EXPECT_EQ(netTypeIfname(NET_WIFI), std::string("wlan0"), "WIFI -> wlan0");
    EXPECT_EQ(netTypeIfname(NET_ETH),  std::string("eth0"),  "ETH -> eth0");
    EXPECT_EQ(netTypeIfname(NET_USB),  std::string("usb0"),  "USB -> usb0");
    EXPECT_EQ(netTypeIfname(NET_INVALID), std::string(""),   "INVALID -> empty");
}

void testIsNetworkUp()
{
    // Mirrors Misc::isWifiConnected: IP non-empty AND gateway non-empty.
    EXPECT_TRUE(isNetworkUp("192.168.1.5", "192.168.1.1"), "ip+gw -> up");
    EXPECT_FALSE(isNetworkUp("", "1.2.3.4"), "empty ip -> down");
    EXPECT_FALSE(isNetworkUp("1.2.3.4", ""), "empty gw -> down");
    EXPECT_FALSE(isNetworkUp("", ""), "empty both -> down");
}

void testEthNeedsConnect()
{
    // Locks "ETH has no connect step" (main_app only setIfname + DHCP).
    EXPECT_FALSE(ethNeedsConnect(), "eth never needs a connect step");
}

void testUsbNeedsStartDefault()
{
    // Locks "main_app baseline never calls start()" — --usb-bringup flips it
    // at the execution layer only.
    EXPECT_FALSE(usbNeedsStartDefault(), "usb does not call start() by default");
}

} // namespace

int main()
{
    // WiFi-specific (ported).
    testDecisionReuse();
    testDecisionReconnect();
    testDecisionReconnectWhenConnectedButNoSsidReported();
    testDecisionFreshConnect();
    testDecisionAbortNoCredentials();
    testDecisionCaseSensitiveSsid();
    testDecisionExitCodeMapping();
    testMayWriteBackGate();
    testNormalizeSsidTrimsOnly();
    testEndToEndDecisionThenGate();

    // T7 uplink-type helpers (new).
    testParseNetType();
    testPtypeToNetType();
    testNetTypeIfname();
    testIsNetworkUp();
    testEthNeedsConnect();
    testUsbNeedsStartDefault();

    if (g_failures == 0) {
        std::printf("test_net_app_logic: ALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_net_app_logic: %d FAILURE(S)\n", g_failures);
    return 1;
}
