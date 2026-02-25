#include "Power.h"
#include <unistd.h>
#include <signal.h>
#include "Logger.h"
#include "misc/Misc.h"
#include "Settings.h"

std::shared_ptr<Power> Power::getInstance()
{
    static std::shared_ptr<Power> instance = nullptr;
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new Power()); });
    return instance;
}

Power::Power()
{
    shutdownRequested = false;
    changeModeRequested = false;
}

Power::~Power()
{
    shutdownRequested = false;
    changeModeRequested = false;
}

void Power::requestShutdown()
{
    shutdownRequested = true;
    // Send SIGTERM signal to current process to notify shutdown
    pid_t pid = getpid();
    Logger::log(LogLevel::INFO, "Sending shutdown signal to process %d", pid);
    kill(pid, SIGTERM); 
}

void Power::requestChangeMode()
{
    changeModeRequested = true;
    Settings::getInstance()->force_upload = 1;
    // Send SIGTERM signal to current process to notify shutdown
    pid_t pid = getpid();
    Logger::log(LogLevel::INFO, "Sending shutdown signal to process %d", pid);
    kill(pid, SIGTERM);
}

bool Power::isShutdownRequested()
{
    return shutdownRequested;
}

bool Power::isChangeModeRequested()
{
    return changeModeRequested;
}