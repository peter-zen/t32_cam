// Unit tests for the time-sync decision logic (manifest §B5, module 3 NTP/RTC).
//
// syncSystemTime() itself is NOT cleanly sim-testable: on the sim host the
// system clock is already >= 2026, so it early-outs at ProcessLifecycle.cpp:669
// and never reaches the RTC/MCU branches; and RTC/MCU have no sdk_stub (they
// gracefully no-op when /dev/rtc0 and /dev/hc32l13x are absent). So the
// meaningful, host-independent targets are the decision gate and the field
// validation those branches depend on:
//   1. TIME_PLAUSIBLE — the gate syncSystemTime / RTC::setTime / syncWithMCU all
//      consult (Common.h:215, PLAUSIBLE_YEAR_MIN=2026).
//   2. RTC::setTime field validation — rejects bad input before any ioctl
//      (RTC.cpp:64-74), host-independent.
//   3. RTC graceful-failure — on a host with no RTC device, getTime/setTime
//      return false instead of crashing (RTC.cpp:77-80,127-130); the contract
//      that makes the sim/sandbox path safe.
//   4. HTC_NO_MCU — MCU public read/write paths short-circuit without touching
//      I2C; time reads return a zeroed (implausible) tm.
//
// Exit: 0 = all pass, non-zero = failure. Build under BUILD_FOR_SIMULATION only.

#include "RTC.h"
#include "MCU.h"
#include "Common.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

namespace {

int g_failures = 0;

#define EXPECT_TRUE(x, msg)                                                    \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::fprintf(stderr, "FAIL [%s]: line %d: %s is false\n",          \
                         msg, __LINE__, #x);                                   \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define EXPECT_FALSE(x, msg)                                                   \
    do {                                                                       \
        if ((x)) {                                                             \
            std::fprintf(stderr, "FAIL [%s]: line %d: %s is true\n",           \
                         msg, __LINE__, #x);                                   \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

struct tm mkTm(int year, int mon, int mday, int hour = 0, int min = 0, int sec = 0)
{
    struct tm t;
    std::memset(&t, 0, sizeof(t));
    t.tm_year = year - YEAR_OFFSET;  // tm_year is years-since-1900
    t.tm_mon = mon;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    return t;
}

// TIME_PLAUSIBLE takes a pointer; bind the by-value tm to a local first so we
// never take the address of an rvalue.
bool plausible(const struct tm& t) { return TIME_PLAUSIBLE(&t); }

// 1) TIME_PLAUSIBLE — the decision gate (PLAUSIBLE_YEAR_MIN=2026).
void testTimePlausible()
{
    // Anything before 2026-01-01 is implausible (the regression guard: a dead
    // RTC battery that resets to 2000 must not be trusted).
    EXPECT_FALSE(plausible(mkTm(2025, 0, 1)), "2025 < 2026 -> implausible");
    EXPECT_FALSE(plausible(mkTm(2000, 0, 1)), "2000 (RTC-reset) -> implausible");

    // 2026-01-01 is the threshold -> plausible.
    EXPECT_TRUE(plausible(mkTm(2026, 0, 1)), "2026 threshold -> plausible");
    EXPECT_TRUE(plausible(mkTm(2026, 5, 15)), "mid-2026 -> plausible");
    EXPECT_TRUE(plausible(mkTm(2100, 11, 31)), "far future -> plausible");

    // A plausible year still fails on bad month/day bounds.
    EXPECT_FALSE(plausible(mkTm(2026, 12, 1)), "mon=12 (>11) -> implausible");
    EXPECT_FALSE(plausible(mkTm(2026, -1, 1)), "mon=-1 (<0) -> implausible");
    EXPECT_FALSE(plausible(mkTm(2026, 0, 0)), "mday=0 (<1) -> implausible");
    EXPECT_FALSE(plausible(mkTm(2026, 0, 32)), "mday=32 (>31) -> implausible");
}

// 2) RTC::setTime field validation — runs before the fd check, so it is
//    host-independent (rejects bad input even with a real /dev/rtc0).
void testRtcSetTimeValidation()
{
    auto rtc = RTC::getInstance();

    // Valid fields but year < YEAR_MIN (2000) -> rejected by validation.
    EXPECT_FALSE(rtc->setTime(mkTm(1999, 0, 1)), "year 1999 < YEAR_MIN -> reject");

    // Out-of-range calendar fields -> rejected.
    EXPECT_FALSE(rtc->setTime(mkTm(2026, 0, 1, 0, 0, 60)), "sec=60 -> reject");
    EXPECT_FALSE(rtc->setTime(mkTm(2026, 0, 1, 24, 0, 0)), "hour=24 -> reject");
    EXPECT_FALSE(rtc->setTime(mkTm(2026, 0, 0)), "mday=0 -> reject");
    EXPECT_FALSE(rtc->setTime(mkTm(2026, 0, 32)), "mday=32 -> reject");
    EXPECT_FALSE(rtc->setTime(mkTm(2026, 12, 1)), "mon=12 -> reject");
}

// 3) RTC graceful-failure — on a host with no RTC device, getTime/setTime must
//    return false (not crash). On a host that DOES expose /dev/rtc0 the result
//    is privilege-dependent, so we only assert when the device is absent.
bool hostHasRtcDevice()
{
    int fd = open("/dev/rtc0", O_RDWR);
    if (fd == -1) fd = open("/dev/rtc", O_RDWR);  // mirror RTC::RTC() fallback
    bool has = (fd != -1);
    if (has) close(fd);
    return has;
}

void testRtcGracefulFailure()
{
    auto rtc = RTC::getInstance();
    struct tm out;

    if (hostHasRtcDevice()) {
        std::printf("note: host exposes /dev/rtc0 — skipping fd-dependent sim-failure "
                    "asserts (privilege-dependent)\n");
        return;
    }
    // No RTC device -> the sim/sandbox contract: false, no crash.
    EXPECT_FALSE(rtc->setTime(mkTm(2026, 0, 1)),
                 "no RTC device -> setTime(valid) returns false (not crash)");
    EXPECT_FALSE(rtc->getTime(out),
                 "no RTC device -> getTime returns false (not crash)");
}

// 4) HTC_NO_MCU — the singleton leaves iic null. Every public method that may
//    be hit by wm startup/shutdown or status APIs must return a default value
//    rather than dereferencing iic.
void testMcuNoMcuShortCircuit()
{
    setenv("HTC_NO_MCU", "1", 1);
    auto mcu = MCU::getInstance();
    struct tm t = mcu->getDatetime();
    EXPECT_FALSE(plausible(t),
                 "MCU::getDatetime implausible with HTC_NO_MCU");
    EXPECT_FALSE(mcu->setDatetime(&t),
                 "MCU::setDatetime returns false with HTC_NO_MCU");
    EXPECT_TRUE(mcu->readPID().empty(), "MCU::readPID empty with HTC_NO_MCU");
    EXPECT_TRUE(mcu->readUPID().empty(), "MCU::readUPID empty with HTC_NO_MCU");
    EXPECT_TRUE(mcu->readUPWD().empty(), "MCU::readUPWD empty with HTC_NO_MCU");
    EXPECT_TRUE(mcu->readSignalType().empty(), "MCU::readSignalType empty with HTC_NO_MCU");
    EXPECT_TRUE(mcu->readDEVICE_NAME().empty(), "MCU::readDEVICE_NAME empty with HTC_NO_MCU");
    EXPECT_TRUE(mcu->readGps() == ",,,,,", "MCU::readGps empty NMEA fields with HTC_NO_MCU");
    EXPECT_TRUE(mcu->readESOR_Value() == 0, "MCU::readESOR_Value zero with HTC_NO_MCU");
}

}  // namespace

int main()
{
    testTimePlausible();
    testRtcSetTimeValidation();
    testRtcGracefulFailure();
    testMcuNoMcuShortCircuit();

    if (g_failures == 0) {
        std::printf("test_ntp: ALL PASS\n");
        return 0;
    }
    std::fprintf(stderr, "test_ntp: %d FAILURE(S)\n", g_failures);
    return 1;
}
