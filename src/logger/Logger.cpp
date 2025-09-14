#include "Logger.h"
#include <cstdarg>
#include <cstdio>
LogLevel Logger::log_level = LogLevel::INFO;

void Logger::log(LogLevel level, const std::string &message)
{
	if (level >= log_level) {
		std::cout << "[" << getLabel(level) << "] " << message
			  << std::endl;
	}
}

void Logger::log(LogLevel level, const char *format, ...)
{
	if (level >= log_level) {
		va_list args;
		va_start(args, format);
		printf("[%s] ", getLabel(level).c_str());
		vprintf(format, args);
		printf("\n");
		va_end(args);
	}
}

void Logger::setLogLevel(LogLevel level)
{
	log_level = level;
}

std::string Logger::getLabel(LogLevel level)
{
	switch (level) {
	case LogLevel::VERBOSE:
		return "VERBOSE";
	case LogLevel::DEBUG:
		return "DEBUG";
	case LogLevel::INFO:
		return "INFO";
	case LogLevel::WARNING:
		return "WARNING";
	case LogLevel::ERROR:
		return "ERROR";
	default:
		return "UNKNOWN";
	}
}