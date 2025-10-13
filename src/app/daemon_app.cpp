// daemon_example.cpp
// 进程守护模块使用示例

#include "daemon_api.h"
#include "Logger.h"
#include "GPIO.h"
#include "Common.h"
#include <unistd.h>

bool shutdownCallback()
{
    Logger::log(LogLevel::INFO, "Executing shutdown callback...");
    
    // 定义需要配置为输入模式的引脚数组
    const int pinsToConfigure[] = {
        POWER_HOLD_PIN,
        IR_CUT_ENABLE_PIN,
        IR_CUT_CTRL_PIN,
        IR_LED_PIN,
        RGB_LED_PIN
    };
    
    // 配置每个引脚为输入模式
    const size_t pinCount = sizeof(pinsToConfigure) / sizeof(pinsToConfigure[0]);
    Logger::log(LogLevel::INFO, "Configuring %zu GPIO pins to input mode...", pinCount);
    
    for (size_t i = 0; i < pinCount; ++i) {
        int pin = pinsToConfigure[i];
        GPIO gpio(pin);
        
        // 检查GPIO是否已导出，如果未导出则尝试导出
        if (!gpio.isExported()) {
            if (gpio.exportGPIO()) {
                Logger::log(LogLevel::INFO, "Exported GPIO pin: %d", pin);
            } else {
                Logger::log(LogLevel::ERROR, "Failed to export GPIO pin: %d", pin);
                continue; // 继续尝试配置下一个引脚
            }
        }
        
        // 配置为输入模式
        if (gpio.setDirection(GPIO_DIRECTION::INPUT)) {
            Logger::log(LogLevel::INFO, "Set GPIO pin %d to INPUT mode successfully", pin);
        } else {
            Logger::log(LogLevel::ERROR, "Failed to set GPIO pin %d to INPUT mode", pin);
        }
    }
    
    Logger::log(LogLevel::INFO, "GPIO pins configuration completed");
    return true;
}

int main(int argc, char* argv[])
{
    
    Logger::log(LogLevel::INFO, "=== Starting Daemon Server Example ===");
    
    if (!registerShutdownCallback(shutdownCallback)) {
        Logger::log(LogLevel::ERROR, "Failed to register shutdown callback");
        return -1;
    }
    
    if (!startDaemonServer()) {
        Logger::log(LogLevel::ERROR, "Failed to start daemon server");
        return -1;
    }
    
    Logger::log(LogLevel::INFO, "Daemon server started, waiting for clients...");
    
    while (1) {
        sleep(1);
    }
    
    stopDaemonServer();
    Logger::log(LogLevel::INFO, "Daemon server stopped");
    
    return 0;
}