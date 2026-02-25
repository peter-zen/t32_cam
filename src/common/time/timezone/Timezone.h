#ifndef TIMEZONE_H
#define TIMEZONE_H
#include <string>
#include <time.h>
#include <mutex>

class Timezone {
    public:
    static bool setTimezone(const std::string& timezone); // "Asia/Shanghai"
    /**
     * @brief 生成包含动态时区偏移的格式化时间字符串
     * @param time_to_format 要格式化的时间戳，默认为当前时间
     * @return 格式化后的时间字符串，格式为"YYYY-MM-DDTHH:MM:SS.000+HH:MM"
     */
    static std::string getFormattedTimeWithTimezone(time_t time_to_format = 0);
    
    private:
    static std::mutex syscall_mutex;
    static bool syscall_inited;
};

#endif // TIMEZONE_H