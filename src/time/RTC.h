#ifndef RTC_H
#define RTC_H

#include <memory>
#include <time.h>

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
};
#endif