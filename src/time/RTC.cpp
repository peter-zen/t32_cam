#include "RTC.h"
#include <memory>
#include <mutex>
#include <time.h>
#include <fstream>
#include "Logger.h"

std::shared_ptr<RTC> RTC::getInstance()
{
	static std::shared_ptr<RTC> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new RTC()); });
	return instance;
}

bool RTC::setTime(const struct tm &time)
{
    time_t raw_time = mktime(const_cast<struct tm*>(&time));
    if (raw_time == -1) {
        Logger::log(LogLevel::ERROR, "Failed to convert tm struct to time_t");
        return false;
    }

    if (stime(&raw_time) == -1) {
        Logger::log(LogLevel::ERROR, "Failed to set system time");
        return false;
    }

    return true;
}

struct tm RTC::getTime()
{
    time_t raw_time;
    struct tm *time_info;
    struct tm time_result;

    time(&raw_time);
    time_info = localtime(&raw_time);
    if (time_info != nullptr) {
        time_result = *time_info;
    }
    return time_result;
}

RTC::RTC()
{
}

RTC::~RTC()
{
}
