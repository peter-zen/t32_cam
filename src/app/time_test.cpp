// time_test — time-chain (RTC / MCU / NTP) HW verification harness.
//
// Models on snap_test: a standalone binary with subcommands that print
// machine-parseable result anchors to stdout ([time_test] op=... k=v ...),
// captured by the devtest broker over serial. Used by tests/host/test_time_chain.py.
//
// Purpose: the wm-app-spec §6/§7 time chain (RTC->MCU->NTP + ntpSynced flag +
// shutdown MCU write-back) is NEW code sitting on sim-only-verified modules
// (mcu/ntp). This harness implements that chain as isolatable functions
// (acquireTimeChain / writebackMcuTime) so it can be HW-verified 1-boot-1-case
// BEFORE being lifted into the wm app's startup path.
//
//   time_test rtc-read                                  raw RTC read (+ read-implies-set probe)
//   time_test rtc-set <Y> <M> <D> <h> <m> <s>           set RTC, read back, round-trip
//   time_test mcu-read                                  raw MCU datetime read
//   time_test mcu-set <Y> <M> <D> <h> <m> <s>           set MCU, read back, round-trip
//   time_test ntp [server:port]                         Misc::ntpSyncAndWait (Stage 2)
//   time_test chain [--ntp-server <s>] [--inject-implausible]   full §6.1 chain
//   time_test writeback [--ntp-synced <0|1>] [--ntp-server <s>] §7 MCU write-back
//
// Dual-platform. On sim RTC gracefully returns false and MCU I2C really fails
// (zeroed tm) — sim only validates compile + graceful paths; the real chain
// value is HW-only (cf. snap_test "Hardware only").

#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <string>
#include <cstdlib>
#include <sys/time.h>
#include "Common.h"            // TIME_PLAUSIBLE, YEAR_OFFSET, MONTH_OFFSET
#include "time/rtc/RTC.h"      // RTC::getInstance
#include "MCU.h"               // MCU::getInstance
#include "misc/Misc.h"         // Misc::ntpSyncAndWait

namespace {

const char* DEFAULT_NTP = "www.aidetcloud.com:123";

// Format a struct tm (tm_year = year-1900, tm_mon 0-11) as ISO-8601 "YYYY-MM-DDThh:mm:ss".
std::string fmtTm(const struct tm& t) {
    std::ostringstream os;
    os << std::setfill('0')
       << (t.tm_year + 1900) << "-"
       << std::setw(2) << (t.tm_mon + 1) << "-"
       << std::setw(2) << (t.tm_mday == 0 ? 1 : t.tm_mday) << "T"
       << std::setw(2) << t.tm_hour << ":"
       << std::setw(2) << t.tm_min << ":"
       << std::setw(2) << t.tm_sec;
    return os.str();
}

std::string fmtSys() {
    time_t now = std::time(nullptr);
    struct tm t;
    if (!localtime_r(&now, &t)) return "none";
    return fmtTm(t);
}

bool sysPlausible() {
    time_t now = std::time(nullptr);
    struct tm t;
    if (!localtime_r(&now, &t)) return false;
    return TIME_PLAUSIBLE(&t) ? true : false;
}

void setSysFromTm(const struct tm& t) {
    struct tm tmp = t;
    time_t sec = std::mktime(&tmp);   // localtime tm -> time_t
    if (sec == static_cast<time_t>(-1)) return;
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

// Parse "<Y> <M> <D> <h> <m> <s>" (1-based month/day) into a tm. Returns false on bad arity.
bool parseTmArgs(int argc, char** argv, struct tm& out) {
    if (argc < 8) return false;
    out = {};
    out.tm_year = std::atoi(argv[2]) - 1900;
    out.tm_mon  = std::atoi(argv[3]) - 1;
    out.tm_mday = std::atoi(argv[4]);
    out.tm_hour = std::atoi(argv[5]);
    out.tm_min  = std::atoi(argv[6]);
    out.tm_sec  = std::atoi(argv[7]);
    return true;
}

// tm difference in seconds (both interpreted in the local timezone). Returns INT64_MIN if invalid.
long long tmDiffSec(const struct tm& a, const struct tm& b) {
    struct tm ta = a, tb = b;
    time_t sa = std::mktime(&ta);
    time_t sb = std::mktime(&tb);
    if (sa == static_cast<time_t>(-1) || sb == static_cast<time_t>(-1)) return -1;
    return static_cast<long long>(sa) - static_cast<long long>(sb);
}

// ---- The chain (spec §6.1). Lifted verbatim into wm once HW-verified. ----
// Order: system(already plausible) -> RTC -> MCU -> NTP. On NTP success set
// system+RTC and ntpSynced=true. Returns the source name; reports side effects.
std::string acquireTimeChain(const std::string& ntpServer,
                             bool& ntpSynced, bool& rtcWritten) {
    ntpSynced = false;
    rtcWritten = false;

    if (sysPlausible()) return "system";

    {
        struct tm t{};
        if (RTC::getInstance()->getTime(t) && TIME_PLAUSIBLE(&t)) {
            setSysFromTm(t);   // redundant (getTime read-implies-set) but explicit/portable
            return "rtc";
        }
    }
    {
        struct tm t = MCU::getInstance()->getDatetime();
        if (TIME_PLAUSIBLE(&t)) {
            setSysFromTm(t);
            return "mcu";
        }
    }
    if (Misc::ntpSyncAndWait(ntpServer) && sysPlausible()) {
        ntpSynced = true;
        time_t now = std::time(nullptr);
        struct tm t;
        if (localtime_r(&now, &t)) {
            rtcWritten = RTC::getInstance()->setTime(t);   // spec: NTP success writes RTC (unconditional)
        }
        return "ntp";
    }
    return "none";
}

// ---- Shutdown MCU write-back (spec §7). Lifted verbatim into wm. ----
// If not yet NTP-synced, try NTP first; then take the (possibly updated) system
// time and write it to MCU, but ONLY if plausible (never persist garbage).
void writebackMcuTime(bool ntpSynced, const std::string& ntpServer,
                      bool& ntpTried, bool& ntpOk,
                      std::string& finalTime, bool& plausible, bool& mcuWritten) {
    ntpTried = false;
    ntpOk = false;
    mcuWritten = false;
    if (!ntpSynced) {
        ntpTried = true;
        ntpOk = Misc::ntpSyncAndWait(ntpServer) && sysPlausible();
    }
    time_t now = std::time(nullptr);
    struct tm t;
    if (!localtime_r(&now, &t)) {
        finalTime = "none";
        plausible = false;
        return;
    }
    finalTime = fmtTm(t);
    plausible = TIME_PLAUSIBLE(&t) ? true : false;
    if (plausible) {
        mcuWritten = MCU::getInstance()->setDatetime(&t);
    }
}

// ---- subcommands ----

int opRtcRead() {
    bool sysPlausBefore = sysPlausible();
    std::string before = fmtSys();
    struct tm t{};
    bool ok = RTC::getInstance()->getTime(t);
    bool sysPlausAfter = sysPlausible();
    std::string after = fmtSys();
    bool plaus = ok && TIME_PLAUSIBLE(&t);
    // read-implies-set gotcha (RTC.cpp: getTime -> set_system_time): did reading
    // an implausible RTC clobber a previously-plausible system clock?
    bool clobber = ok && sysPlausBefore && !sysPlausAfter;
    std::cout << "[time_test] op=rtc-read rtc_ok=" << (ok ? 1 : 0)
              << " rtc=" << (ok ? fmtTm(t) : std::string("none"))
              << " plausible=" << (plaus ? 1 : 0)
              << " sys_before=" << before
              << " sys_after=" << after
              << " sys_plausible_before=" << (sysPlausBefore ? 1 : 0)
              << " sys_plausible_after=" << (sysPlausAfter ? 1 : 0)
              << " clobber=" << (clobber ? 1 : 0) << std::endl;
    return ok ? 0 : 1;
}

int opRtcSet(int argc, char** argv) {
    struct tm t;
    if (!parseTmArgs(argc, argv, t)) {
        std::cerr << "usage: time_test rtc-set <Y> <M> <D> <h> <m> <s>\n";
        return 2;
    }
    std::string setStr = fmtTm(t);
    bool ok = RTC::getInstance()->setTime(t);
    struct tm rb{};
    bool rbOk = RTC::getInstance()->getTime(rb);
    long long diff = rbOk ? tmDiffSec(t, rb) : -1;
    bool match = ok && rbOk && diff >= 0 && diff <= 3;
    std::cout << "[time_test] op=rtc-set set=" << setStr
              << " write_ok=" << (ok ? 1 : 0)
              << " readback=" << (rbOk ? fmtTm(rb) : std::string("none"))
              << " match=" << (match ? 1 : 0) << std::endl;
    return (ok && match) ? 0 : 1;
}

int opMcuRead() {
    struct tm t = MCU::getInstance()->getDatetime();
    bool plaus = TIME_PLAUSIBLE(&t) ? true : false;
    bool zeroed = (t.tm_year == 0 && t.tm_mon == 0 && t.tm_mday == 0 &&
                   t.tm_hour == 0 && t.tm_min == 0 && t.tm_sec == 0);
    std::cout << "[time_test] op=mcu-read mcu=" << fmtTm(t)
              << " plausible=" << (plaus ? 1 : 0)
              << " zeroed=" << (zeroed ? 1 : 0) << std::endl;
    return 0;  // report honestly regardless (can't distinguish I2C-fail from real-1900)
}

int opMcuSet(int argc, char** argv) {
    struct tm t;
    if (!parseTmArgs(argc, argv, t)) {
        std::cerr << "usage: time_test mcu-set <Y> <M> <D> <h> <m> <s>\n";
        return 2;
    }
    std::string setStr = fmtTm(t);
    bool ok = MCU::getInstance()->setDatetime(&t);
    struct tm rb = MCU::getInstance()->getDatetime();
    long long diff = tmDiffSec(t, rb);
    bool match = ok && diff >= 0 && diff <= 3;
    std::cout << "[time_test] op=mcu-set set=" << setStr
              << " write_ok=" << (ok ? 1 : 0)
              << " readback=" << fmtTm(rb)
              << " match=" << (match ? 1 : 0) << std::endl;
    return (ok && match) ? 0 : 1;
}

int opNtp(int argc, char** argv) {
    std::string server = (argc > 2) ? argv[2] : DEFAULT_NTP;
    std::string before = fmtSys();
    bool ok = Misc::ntpSyncAndWait(server);
    std::string after = fmtSys();
    std::cout << "[time_test] op=ntp server=" << server
              << " result=" << (ok ? "ok" : "fail")
              << " sys_before=" << before
              << " sys_after=" << after
              << " plausible=" << (sysPlausible() ? 1 : 0) << std::endl;
    return ok ? 0 : 1;
}

int opChain(int argc, char** argv) {
    std::string server = DEFAULT_NTP;
    bool inject = false;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--inject-implausible") {
            inject = true;
        } else if (a == "--ntp-server" && i + 1 < argc) {
            server = argv[++i];
        }
    }
    if (inject) {
        // NOTE: on T32 this is unreliable — the kernel re-syncs sys<-RTC, so
        // settimeofday(1970) is reverted while RTC is plausible (confirmed
        // 2026-06-23). Kept as a diagnostic; do NOT rely on it to force the
        // fallback. Verify the RTC-fallback by observing a real cold boot.
        struct timeval tv;
        tv.tv_sec = 0;          // 1970-01-01 — force the chain through RTC/MCU/NTP
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);
    }
    std::string before = fmtSys();
    bool ntpSynced = false, rtcWritten = false;
    std::string source = acquireTimeChain(server, ntpSynced, rtcWritten);
    std::cout << "[time_test] op=chain inject=" << (inject ? 1 : 0)
              << " source=" << source
              << " ntp_synced=" << (ntpSynced ? 1 : 0)
              << " rtc_written=" << (rtcWritten ? 1 : 0)
              << " sys_before=" << before
              << " sys_after=" << fmtSys()
              << " plausible=" << (sysPlausible() ? 1 : 0) << std::endl;
    return 0;  // any source (incl. none) is a valid, reportable outcome
}

int opWriteback(int argc, char** argv) {
    int ntpSyncedIn = 0;
    std::string server = DEFAULT_NTP;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--ntp-synced" && i + 1 < argc) {
            ntpSyncedIn = std::atoi(argv[++i]);
        } else if (a == "--ntp-server" && i + 1 < argc) {
            server = argv[++i];
        }
    }
    bool ntpTried = false, ntpOk = false, plausible = false, mcuWritten = false;
    std::string finalTime;
    writebackMcuTime(ntpSyncedIn != 0, server, ntpTried, ntpOk, finalTime, plausible, mcuWritten);
    std::cout << "[time_test] op=writeback ntp_synced_in=" << ntpSyncedIn
              << " ntp_tried=" << (ntpTried ? 1 : 0)
              << " ntp_ok=" << (ntpOk ? 1 : 0)
              << " final=" << finalTime
              << " plausible=" << (plausible ? 1 : 0)
              << " mcu_written=" << (mcuWritten ? 1 : 0) << std::endl;
    return 0;
}

void usage(const char* prog) {
    std::cerr << "usage: " << prog << " <rtc-read|rtc-set Y M D h m s|mcu-read|"
              << "mcu-set Y M D h m s|ntp [server:port]|chain [--ntp-server s] "
              << "[--inject-implausible]|writeback [--ntp-synced 0|1] [--ntp-server s]>\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "rtc-read")  return opRtcRead();
    if (cmd == "rtc-set")   return opRtcSet(argc, argv);
    if (cmd == "mcu-read")  return opMcuRead();
    if (cmd == "mcu-set")   return opMcuSet(argc, argv);
    if (cmd == "ntp")       return opNtp(argc, argv);
    if (cmd == "chain")     return opChain(argc, argv);
    if (cmd == "writeback") return opWriteback(argc, argv);
    if (cmd == "-h" || cmd == "--help") {
        usage(argv[0]);
        return 0;
    }
    std::cerr << "unknown subcommand: " << cmd << "\n";
    usage(argv[0]);
    return 2;
}
