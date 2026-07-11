
#include <mutex>
#include <string>
#include <ctime>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include "Logger.h"
#include "Timezone.h"

// 初始化静态成员变量
std::mutex Timezone::syscall_mutex;
bool Timezone::syscall_inited = false;

/**
 * @brief 生成包含动态时区偏移的格式化时间字符串
 * @param time_to_format 要格式化的时间戳，默认为当前时间
 * @return 格式化后的时间字符串，格式为"YYYY-MM-DDTHH:MM:SS.000+HH:MM"
 */
std::string Timezone::getFormattedTimeWithTimezone(time_t time_to_format)
{
    if (time_to_format == 0) {
        time_to_format = time(nullptr);  // 使用当前时间
    }
    
    // 获取本地时间
    struct tm tm_info_copy;
    struct tm* tm_info = localtime_r(&time_to_format, &tm_info_copy);
    
    if (!tm_info) {
        Logger::log(LogLevel::ERROR, "Failed to get local time");
        return ""; 
    }
    
    // 获取UTC时间
    struct tm gm_time_copy;
    struct tm* gm_time = gmtime_r(&time_to_format, &gm_time_copy);
    
    if (!gm_time) {
        Logger::log(LogLevel::ERROR, "Failed to get UTC time");
        return ""; 
    }
    
    // 更可靠的时区偏移计算方法
    // 1. 先获取UTC时间对应的本地时间戳
    time_t local_time_for_utc = std::mktime(gm_time);
    
    // 2. 计算偏移量（UTC时间对应的本地时间戳 - UTC时间戳）
    int offset_seconds = std::difftime(time_to_format, local_time_for_utc);
    
    // Convert offset to hours and minutes
    int offset_hours = offset_seconds / 3600;
    int offset_minutes = abs((offset_seconds % 3600) / 60);
    
    // Format the timezone offset string (+/-HH:MM)
    char tz_offset[10];
    snprintf(tz_offset, sizeof(tz_offset), "%+03d:%02d", offset_hours, offset_minutes);
    
    // Create format string with dynamic timezone offset
    char format[64];
    snprintf(format, sizeof(format), "%%Y-%%m-%%dT%%H:%%M:%%S.000%s", tz_offset);
    
    // Format the time
    char tmp_buffer[64];
    if (!std::strftime(tmp_buffer, sizeof(tmp_buffer), format, tm_info)) {
        Logger::log(LogLevel::ERROR, "Failed to format time with timezone");
        return "";
    }
    
    return tmp_buffer;
}

bool Timezone::setTimezone(const std::string& timezone)
{
    // 反转时区字符串中的符号
    std::string adjusted_timezone = timezone;
    
    // 处理UTC+8或UTC-8格式
    size_t plus_pos = adjusted_timezone.find("UTC+");
    size_t minus_pos = adjusted_timezone.find("UTC-");
    
    if (plus_pos != std::string::npos) {
        // 将UTC+转换为UTC-
        adjusted_timezone[plus_pos + 3] = '-';
    } else if (minus_pos != std::string::npos) {
        // 将UTC-转换为UTC+
        adjusted_timezone[minus_pos + 3] = '+';
    }
    // UTC格式不需要修改
    
    // 设置调整后的TZ环境变量并刷新
    setenv("TZ", adjusted_timezone.c_str(), 1);
    tzset();

#ifndef BUILD_FOR_SIMULATION
    // 持久化到 /etc/TZ：同一开机周期内的兄弟/后续进程在 TZ 环境变量未设时，
    // uClibc 会自动读取 /etc/TZ（回退顺序：TZ env → /etc/TZ → UTC），从而
    // elog/localtime 从进程第一行起就是本地时区，无需每个进程各自重新设置。
    // 根 "/" 是可写 rootfs(内存盘)，root 可写；best-effort，失败静默。注意
    // 内存盘重启清空：每次开机由首个设置时区的 app 重新写入（幂等）。
    // 写入带换行，与 echo > /etc/TZ 实测过的格式一致。
    std::ofstream tz_file("/etc/TZ");
    if (tz_file) {
        tz_file << adjusted_timezone << "\n";
    }
#endif

    return true;
}