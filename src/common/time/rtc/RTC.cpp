#include "RTC.h"
#include <memory>
#include <mutex>
#include <time.h>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/rtc.h>
#include <string.h>
#include "Logger.h"
#include "Common.h"

// stime() is deprecated in glibc 2.31+, use clock_settime() instead
static inline int set_system_time(time_t *t) {
#if defined(SIMULATION_MODE) || (__GLIBC__ > 2) || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 31)
    struct timespec ts;
    ts.tv_sec = *t;
    ts.tv_nsec = 0;
    return clock_settime(CLOCK_REALTIME, &ts);
#else
    return stime(t);
#endif
}

std::shared_ptr<RTC> RTC::getInstance()
{
	static std::shared_ptr<RTC> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new RTC()); });
	return instance;
}

RTC::RTC() : m_rtc_fd(-1), m_rtc_device("/dev/rtc0")
{
    // Try to open the RTC device
    m_rtc_fd = open(m_rtc_device.c_str(), O_RDWR);
    if (m_rtc_fd == -1) {
        // Try alternative path if primary fails
        m_rtc_device = "/dev/rtc";
        m_rtc_fd = open(m_rtc_device.c_str(), O_RDWR);
        if (m_rtc_fd == -1) {
            Logger::log(LogLevel::WARNING, "Failed to open RTC device. System time will be used instead.");
        } else {
            Logger::log(LogLevel::INFO, "Successfully opened RTC device: %s", m_rtc_device.c_str());
        }
    } else {
        Logger::log(LogLevel::INFO, "Successfully opened RTC device: %s", m_rtc_device.c_str());
    }
}

RTC::~RTC()
{
    // Close the RTC device if it was opened
    if (m_rtc_fd != -1) {
        close(m_rtc_fd);
        Logger::log(LogLevel::INFO, "RTC device closed: %s", m_rtc_device.c_str());
        m_rtc_fd = -1;
    }
}

bool RTC::setTime(const struct tm &time) {
    // Validate input time parameters
    if (time.tm_sec < 0 || time.tm_sec > 59 ||
        time.tm_min < 0 || time.tm_min > 59 ||
        time.tm_hour < 0 || time.tm_hour > 23 ||
        time.tm_mday < 1 || time.tm_mday > 31 ||
        time.tm_mon < 0 || time.tm_mon > 11 ||
        time.tm_year < YEAR_MIN-YEAR_OFFSET) {  // Year must be at least 2000 (100 in tm_year format)
        Logger::log(LogLevel::ERROR, "Invalid time parameters: %04d-%02d-%02d %02d:%02d:%02d",
                   time.tm_year + YEAR_OFFSET, time.tm_mon + MONTH_OFFSET, time.tm_mday,
                   time.tm_hour, time.tm_min, time.tm_sec);
        return false;
    }

    // Check if RTC device is available
    if (m_rtc_fd == -1) {
        Logger::log(LogLevel::ERROR, "RTC device is not available: %s", m_rtc_device.c_str());
        return false;
    }
    struct rtc_time rtc_tm;
    
    // Convert tm to rtc_time format
    memset(&rtc_tm, 0, sizeof(rtc_tm));
    rtc_tm.tm_sec = time.tm_sec;
    rtc_tm.tm_min = time.tm_min;
    rtc_tm.tm_hour = time.tm_hour;
    rtc_tm.tm_mday = time.tm_mday;
    rtc_tm.tm_mon = time.tm_mon;
    rtc_tm.tm_year = time.tm_year;
    rtc_tm.tm_wday = time.tm_wday;
    rtc_tm.tm_yday = time.tm_yday;
    rtc_tm.tm_isdst = time.tm_isdst;
    
    // Log the time being set to RTC
    Logger::log(LogLevel::DEBUG, "Setting RTC time: %04d-%02d-%02d %02d:%02d:%02d",
                rtc_tm.tm_year + YEAR_OFFSET, rtc_tm.tm_mon + MONTH_OFFSET, rtc_tm.tm_mday,
                rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
    
    // Set RTC time using ioctl
    if (ioctl(m_rtc_fd, RTC_SET_TIME, &rtc_tm) == -1) {
        Logger::log(LogLevel::ERROR, "Failed to set RTC time: %s (errno=%d)", 
                    strerror(errno), errno);
        Logger::log(LogLevel::ERROR, "Time parameters causing error: %04d-%02d-%02d %02d:%02d:%02d",
                    rtc_tm.tm_year + YEAR_OFFSET, rtc_tm.tm_mon + MONTH_OFFSET, rtc_tm.tm_mday,
                    rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);
        
        return false;
    }
    
    Logger::log(LogLevel::INFO, "RTC time set successfully");
    
    // Also update system time for consistency
    time_t raw_time = mktime(const_cast<struct tm*>(&time));
    if (raw_time != -1) {
        set_system_time(&raw_time);
    }
    
    return true;
}

bool RTC::getTime(struct tm& time) {
    // Initialize the time structure to zero
    memset(&time, 0, sizeof(time));
    
    // Check if RTC device is available and valid
    if (m_rtc_fd == -1) {
        Logger::log(LogLevel::WARNING, "RTC device is not available: %s", m_rtc_device.c_str());
        return false;
    }

    struct rtc_time rtc_tm;
    // Ensure rtc_tm is properly initialized
    memset(&rtc_tm, 0, sizeof(rtc_tm));
    
    // Get RTC time using ioctl
    if (ioctl(m_rtc_fd, RTC_RD_TIME, &rtc_tm) == -1) {
        Logger::log(LogLevel::ERROR, "Failed to read RTC time: %s (errno=%d)", 
                   strerror(errno), errno);

        return false;
    }
    
    // Convert rtc_time to tm format
    time.tm_sec = rtc_tm.tm_sec;
    time.tm_min = rtc_tm.tm_min;
    time.tm_hour = rtc_tm.tm_hour;
    time.tm_mday = rtc_tm.tm_mday;
    time.tm_mon = rtc_tm.tm_mon;
    time.tm_year = rtc_tm.tm_year;
    time.tm_wday = rtc_tm.tm_wday;
    time.tm_yday = rtc_tm.tm_yday;
    time.tm_isdst = rtc_tm.tm_isdst;
    
    Logger::log(LogLevel::DEBUG, "Read time from RTC successfully: %04d-%02d-%02d %02d:%02d:%02d",
               time.tm_year, time.tm_mon + 1, time.tm_mday,
               time.tm_hour, time.tm_min, time.tm_sec);

    // Also update system time for consistency
    time_t raw_time = mktime(const_cast<struct tm*>(&time));
    if (raw_time != -1) {
        set_system_time(&raw_time);
    }

    return true;
}
