#ifndef RTC_H
#define RTC_H

#include <memory>
#include <time.h>
#include <string>

class RTC {
    public:
	static std::shared_ptr<RTC> getInstance();
	/**
	 * @brief Sets the RTC hardware clock to the specified time
	 * 
	 * This function validates the input time parameters, converts them to RTC format,
	 * and writes the time to the RTC hardware device. If successful, it also updates
	 * the system time for consistency.
	 * 
	 * @note In the time structure, year and mon should be offsetted values:
	 *       - year: Full year (e.g., 125 for the year 2025)
	 *       - mon: Month number from 0 to 11 (e.g., 0 for January, 11 for December)
	 * 
	 * @param time The time structure containing the date and time to set
	 * @return true on successful time setting, false on error
	 */
	bool setTime(const struct tm &time);
	/**
	 * @brief Retrieves the current time from the RTC hardware clock
	 * 
	 * This function reads the current time from the RTC hardware device and
	 * stores it in the provided tm structure.
	 * 
	 * @note The returned time structure contains offsetted values:
	 *       - year: Full year (e.g., 125 for the year 2025)
	 *       - mon: Month number from 0 to 11 (e.g., 0 for January, 11 for December)
	 * 
	 * @param time Reference to a tm structure to store the retrieved time
	 * @return true on successful time retrieval, false on error
	 */
	bool getTime(struct tm &time);
	~RTC();

    private:
	RTC();
	RTC(const RTC &) = delete;
	RTC &operator=(const RTC &) = delete;
    
	int m_rtc_fd;           // RTC device file descriptor
	std::string m_rtc_device;  // RTC device path
};
#endif