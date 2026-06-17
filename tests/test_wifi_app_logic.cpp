// Unit tests for the pure-logic layer of htc_wifi_app.
//
// Covers (planner T6 §8.1):
//   - decide(): ABORT / FRESH_CONNECT / RECONNECT / REUSE
//   - decisionExitCode(): Decision -> baseline exit code mapping
//   - mayWriteBack(): strict gate (connected + live SSID == target)
//   - normalizeSsid(): trim only, case-sensitive preserved
//
// This translation unit has NO system calls; it links only against
// wifi_app_logic. Build under BUILD_FOR_SIMULATION only.
//
// Exit: 0 = all pass, non-zero = failure.

#include "wifi_app_logic.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace wifi_app_logic;

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

} // namespace

int main()
{
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

    if (g_failures == 0) {
        std::printf("test_wifi_app_logic: ALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_wifi_app_logic: %d FAILURE(S)\n", g_failures);
    return 1;
}
