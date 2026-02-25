/*
 * Logger.cpp - Compatibility layer implementation wrapping EasyLogger
 * 
 * This implementation forwards all Logger calls to EasyLogger.
 * New code should use EasyLogger API directly.
 */

#include "Logger.h"
#include <cstdarg>
#include <cstdio>
#include <elog.h>

// Default tag for legacy Logger calls
const char* Logger::TAG = "LEGACY";

void Logger::setLogLevel(LogLevel level)
{
    // Map LogLevel to EasyLogger filter level
    elog_set_filter_lvl(static_cast<uint8_t>(level));
}

void Logger::log(LogLevel level, const std::string &message)
{
    // Forward to EasyLogger based on level
    switch (level) {
    case LogLevel::ERROR:
        elog_e(TAG, "%s", message.c_str());
        break;
    case LogLevel::WARNING:
        elog_w(TAG, "%s", message.c_str());
        break;
    case LogLevel::INFO:
        elog_i(TAG, "%s", message.c_str());
        break;
    case LogLevel::DEBUG:
        elog_d(TAG, "%s", message.c_str());
        break;
    case LogLevel::VERBOSE:
        elog_v(TAG, "%s", message.c_str());
        break;
    default:
        elog_i(TAG, "%s", message.c_str());
        break;
    }
}

void Logger::log(LogLevel level, const char *format, ...)
{
    // Format the message first
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Forward to EasyLogger based on level
    switch (level) {
    case LogLevel::ERROR:
        elog_e(TAG, "%s", buffer);
        break;
    case LogLevel::WARNING:
        elog_w(TAG, "%s", buffer);
        break;
    case LogLevel::INFO:
        elog_i(TAG, "%s", buffer);
        break;
    case LogLevel::DEBUG:
        elog_d(TAG, "%s", buffer);
        break;
    case LogLevel::VERBOSE:
        elog_v(TAG, "%s", buffer);
        break;
    default:
        elog_i(TAG, "%s", buffer);
        break;
    }
}
