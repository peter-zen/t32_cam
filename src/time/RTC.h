#ifndef RTC_H
#define RTC_H

#include <memory>
#include <time.h>
#include <string>

class RTC {
    public:
	static std::shared_ptr<RTC> getInstance();
	bool setTime(const struct tm &time);
	struct tm getTime();
	~RTC();

    private:
	RTC();
	RTC(const RTC &) = delete;
	RTC &operator=(const RTC &) = delete;
    
	int m_rtc_fd;           // RTC device file descriptor
	std::string m_rtc_device;  // RTC device path
};
#endif