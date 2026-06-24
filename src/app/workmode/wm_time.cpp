// wm_time — see wm_time.h. Lifted verbatim (logic-wise) from time_test.cpp's
// acquireTimeChain / writebackMcuTime, which are HW-verified (2026-06-23).

#include "wm_time.h"

#include <ctime>
#include <sys/time.h>

#include "Common.h"        // TIME_PLAUSIBLE, YEAR_OFFSET
#include "RTC.h"           // RTC::getInstance
#include "MCU.h"           // MCU::getInstance
#include "misc/Misc.h"     // Misc::ntpSyncAndWait
#include "Logger.h"

namespace {

bool sysPlausible() {
    time_t now = std::time(nullptr);
    struct tm t;
    if (!localtime_r(&now, &t)) return false;
    return TIME_PLAUSIBLE(&t) ? true : false;
}

void setSysFromTm(const struct tm& t) {
    struct tm tmp = t;
    time_t sec = std::mktime(&tmp);
    if (sec == static_cast<time_t>(-1)) return;
    struct timeval tv;
    tv.tv_sec = sec;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
}

}  // namespace

namespace app_workmode {

std::string acquireTimeChain(const std::string& ntpServer, bool& ntpSynced) {
    ntpSynced = false;

    if (sysPlausible()) return "system";

    {
        struct tm t{};
        // 注：RTC::getTime 成功会 read-implies-set（RTC.cpp:162）；此处仅在系统不可信时
        // 才读 RTC，故不会把好时钟被坏 RTC 覆盖（spec §6.1 step1 gate）。
        if (RTC::getInstance()->getTime(t) && TIME_PLAUSIBLE(&t)) {
            setSysFromTm(t);
            return "rtc";
        }
    }
    {
        struct tm t = MCU::getInstance()->getDatetime();  // I2C 失败返零 tm（implausible）
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
            RTC::getInstance()->setTime(t);  // spec：NTP 成功无条件写 system+RTC
        }
        return "ntp";
    }
    return "none";
}

void writebackMcuTime(bool ntpSynced, const std::string& ntpServer) {
    if (!ntpSynced) {
        // 先补一次 NTP（在线则成功；离线/m0 无网则失败，用当前 sys 时间）。
        Misc::ntpSyncAndWait(ntpServer);
    }
    time_t now = std::time(nullptr);
    struct tm t;
    if (!localtime_r(&now, &t)) {
        Logger::log(LogLevel::WARNING, "[wm] writebackMcuTime: localtime_r failed, skip");
        return;
    }
    if (!TIME_PLAUSIBLE(&t)) {
        Logger::log(LogLevel::WARNING, "[wm] writebackMcuTime: system time implausible, skip MCU write");
        return;
    }
    if (!MCU::getInstance()->setDatetime(&t)) {
        Logger::log(LogLevel::WARNING, "[wm] writebackMcuTime: MCU::setDatetime failed");
    } else {
        Logger::log(LogLevel::INFO, "[wm] writebackMcuTime: wrote MCU time (ntpSynced=%d)",
                    ntpSynced ? 1 : 0);
    }
}

}  // namespace app_workmode
