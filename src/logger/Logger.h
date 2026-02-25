/*
 * Logger.h - Compatibility layer wrapping EasyLogger
 * 
 * This file provides backward compatibility with the original Logger API.
 * New code should use EasyLogger API directly (elog_i, elog_d, elog_e, etc.)
 * 
 * Migration guide:
 *   Logger::log(LogLevel::INFO, "message")  ->  elog_i("TAG", "message")
 *   Logger::log(LogLevel::DEBUG, "msg %d", x) -> elog_d("TAG", "msg %d", x)
 *   Logger::setLogLevel(LogLevel::DEBUG)    ->  elog_set_filter_lvl(ELOG_LVL_DEBUG)
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <elog.h>

/* 
 * LogLevel enum - maps to EasyLogger levels
 * Kept for backward compatibility with existing code
 */
enum class LogLevel { 
    VERBOSE = ELOG_LVL_VERBOSE,  // 5
    DEBUG = ELOG_LVL_DEBUG,      // 4
    INFO = ELOG_LVL_INFO,        // 3
    WARNING = ELOG_LVL_WARN,     // 2
    ERROR = ELOG_LVL_ERROR       // 1
};

/*
 * Logger class - Compatibility wrapper for EasyLogger
 * 
 * [[deprecated]] attribute will generate compiler warnings to encourage
 * migration to native EasyLogger API.
 */
class 
#if __cplusplus >= 201402L
[[deprecated("Use EasyLogger API (elog_i, elog_d, elog_e, etc.) instead. See Logger.h for migration guide.")]]
#endif
Logger {
public:
    /**
     * Set global log level filter
     * @param level Minimum level to output
     */
    static void setLogLevel(LogLevel level);
    
    /**
     * Log a message with specified level
     * @param level Log level
     * @param message Log message string
     */
    static void log(LogLevel level, const std::string &message);
    
    /**
     * Log a formatted message with specified level
     * @param level Log level
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    static void log(LogLevel level, const char *format, ...);

private:
    static const char* TAG;  // Default tag for legacy logs
};

#endif // LOGGER_H
