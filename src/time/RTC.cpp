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

bool RTC::setTime(const struct tm &time)
{
    // Check if RTC device is available
    if (m_rtc_fd != -1) {
        struct rtc_time rtc_tm;
        
        // Convert tm to rtc_time format
        memset(&rtc_tm, 0, sizeof(rtc_tm));
        rtc_tm.tm_sec = time.tm_sec;
        rtc_tm.tm_min = time.tm_min;
        rtc_tm.tm_hour = time.tm_hour;
        rtc_tm.tm_mday = time.tm_mday;
        rtc_tm.tm_mon = time.tm_mon;  // Note: Linux RTC expects 0-11 for month
        rtc_tm.tm_year = time.tm_year - 1900;  // Years since 1900
        rtc_tm.tm_wday = time.tm_wday;
        rtc_tm.tm_yday = time.tm_yday;
        rtc_tm.tm_isdst = time.tm_isdst;
        
        // Set RTC time using ioctl
        if (ioctl(m_rtc_fd, RTC_SET_TIME, &rtc_tm) == -1) {
            Logger::log(LogLevel::ERROR, "Failed to set RTC time: %s", strerror(errno));
            
            // Fallback to system time
            time_t raw_time = mktime(const_cast<struct tm*>(&time));
            if (raw_time == -1) {
                Logger::log(LogLevel::ERROR, "Failed to convert tm struct to time_t");
                return false;
            }
            
            if (stime(&raw_time) == -1) {
                Logger::log(LogLevel::ERROR, "Failed to set system time as fallback");
                return false;
            }
            
            Logger::log(LogLevel::WARNING, "Using system time as fallback for RTC");
            return true;
        }
        
        Logger::log(LogLevel::INFO, "RTC time set successfully");
        
        // Also update system time for consistency
        time_t raw_time = mktime(const_cast<struct tm*>(&time));
        if (raw_time != -1) {
            stime(&raw_time);
        }
        
        return true;
    } else {
        // Fallback to system time if RTC device not available
        time_t raw_time = mktime(const_cast<struct tm*>(&time));
        if (raw_time == -1) {
            Logger::log(LogLevel::ERROR, "Failed to convert tm struct to time_t");
            return false;
        }
        
        if (stime(&raw_time) == -1) {
            Logger::log(LogLevel::ERROR, "Failed to set system time");
            return false;
        }
        
        Logger::log(LogLevel::WARNING, "Using system time as fallback (RTC device not available)");
        return true;
    }
}

struct tm RTC::getTime()
{
    struct tm time_result;
    memset(&time_result, 0, sizeof(time_result));
    
    // Check if RTC device is available
    if (m_rtc_fd != -1) {
        struct rtc_time rtc_tm;
        
        // Get RTC time using ioctl
        if (ioctl(m_rtc_fd, RTC_RD_TIME, &rtc_tm) == -1) {
            Logger::log(LogLevel::ERROR, "Failed to read RTC time: %s", strerror(errno));
            
            // Fallback to system time
            time_t raw_time;
            time(&raw_time);
            struct tm *time_info = localtime(&raw_time);
            if (time_info != nullptr) {
                time_result = *time_info;
            }
            
            Logger::log(LogLevel::WARNING, "Using system time as fallback for RTC read");
            return time_result;
        }
        
        // Convert rtc_time to tm format
        time_result.tm_sec = rtc_tm.tm_sec;
        time_result.tm_min = rtc_tm.tm_min;
        time_result.tm_hour = rtc_tm.tm_hour;
        time_result.tm_mday = rtc_tm.tm_mday;
        time_result.tm_mon = rtc_tm.tm_mon;  // Note: Both use 0-11 for month
        time_result.tm_year = rtc_tm.tm_year + 1900;  // Convert to years since 1900
        time_result.tm_wday = rtc_tm.tm_wday;
        time_result.tm_yday = rtc_tm.tm_yday;
        time_result.tm_isdst = rtc_tm.tm_isdst;
        
        Logger::log(LogLevel::DEBUG, "Read time from RTC successfully");
        return time_result;
    } else {
        // Fallback to system time if RTC device not available
        time_t raw_time;
        time(&raw_time);
        struct tm *time_info = localtime(&raw_time);
        if (time_info != nullptr) {
            time_result = *time_info;
        }
        
        Logger::log(LogLevel::WARNING, "Using system time as fallback (RTC device not available)");
        return time_result;
    }
}
