#include "Power.h"
#include <memory>
#include <mutex>
#include <csignal>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>
#include "Logger.h"
#include "Misc.h"

std::shared_ptr<Power> Power::getInstance()
{
    static std::shared_ptr<Power> instance = nullptr;
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new Power()); });
    return instance;
}

Power::Power()
{
}

Power::~Power()
{
}

void Power::requestShutdown()
{
    // Send SIGTERM signal to current process to notify shutdown
    pid_t pid = getpid();
    Logger::log(LogLevel::INFO, "Sending shutdown signal to process %d", pid);
    kill(pid, SIGTERM); 
}
