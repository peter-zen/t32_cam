#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <string>

enum class LogLevel { VERBOSE, DEBUG, INFO, WARNING, ERROR };

class Logger {
    public:
	static void setLogLevel(LogLevel level);
	static void log(LogLevel level, const std::string &message);
	static void log(LogLevel level, const char *format, ...);

    private:
	static LogLevel log_level;
	static std::string getLabel(LogLevel level);
};

#endif // LOGGER_H