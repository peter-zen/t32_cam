#include "GPIO.h"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <cmath>
#include <chrono>
#include <thread>
#include "Logger.h"
#include "utils/string/StringConvert.h"

#include "IGpio.h"
#include "HalProvider.h"

// GPIO sysfs基本路径 (仅真机模式使用)
#ifndef BUILD_FOR_SIMULATION
#define SYSFS_GPIO_PATH "/sys/class/gpio"
#endif

GPIO::GPIO(int num) : 
    m_gpioNum(num), 
    m_isExported(false),
    m_currentFrequency(1.0),
    m_currentDuration(0),
    m_currentDutyCycle(0.5)
{
    m_gpioPath = "";
    m_halGpio = hal::HalProvider::createGpio();
    
    // 初始化成员变量
    m_blinkActive = false;
    m_blinkMutex = std::make_shared<std::mutex>();
    // m_blinkThread将在需要时初始化
}

GPIO::~GPIO()
{
    // 析构时停止闪烁线程并等待其结束
    stopBlink();
    
    // 析构时自动取消导出GPIO
    if (m_isExported) {
        unexportGPIO();
    }
}

GPIO::GPIO(const GPIO& other) :
    m_gpioNum(other.m_gpioNum),
    m_isExported(false), // 新对象不共享导出状态
    m_gpioPath(other.m_gpioPath),
    m_currentFrequency(other.m_currentFrequency),
    m_currentDuration(other.m_currentDuration),
    m_currentDutyCycle(other.m_currentDutyCycle)
{
    // 创建新的互斥锁，每个对象有自己独立的线程和互斥锁
    m_blinkActive = false;
    m_blinkMutex = std::make_shared<std::mutex>();
    m_blinkThread = nullptr;
}

GPIO& GPIO::operator=(const GPIO& other)
{
    if (this != &other) {
        // 先停止当前对象的闪烁线程
        stopBlink();
        
        // 取消导出当前对象的GPIO
        if (m_isExported) {
            unexportGPIO();
        }
        
        // 复制基本成员
        m_gpioNum = other.m_gpioNum;
        m_isExported = false; // 新对象不共享导出状态
        m_gpioPath = other.m_gpioPath;
        m_currentFrequency = other.m_currentFrequency;
        m_currentDuration = other.m_currentDuration;
        m_currentDutyCycle = other.m_currentDutyCycle;
        
        // 创建新的互斥锁，每个对象有自己独立的线程和互斥锁
        m_blinkActive = false;
        m_blinkMutex = std::make_shared<std::mutex>();
        m_blinkThread = nullptr;
    }
    return *this;
}

bool GPIO::exportGPIO()
{
    // 检查是否已经导出
    if (m_isExported) {
        return true;
    }

    if (m_halGpio && m_halGpio->exportPin(m_gpioNum)) {
        m_isExported = true;
        return true;
    }
    return false;
}

bool GPIO::unexportGPIO()
{
    // 检查是否已经取消导出
    if (!m_isExported) {
        return true;
    }

    if (m_halGpio && m_halGpio->unexportPin(m_gpioNum)) {
        m_isExported = false;
        return true;
    }
    return false;
}

bool GPIO::setDirection(GPIO_DIRECTION dir)
{
    // 检查是否已导出
    if (!m_isExported && !exportGPIO()) {
        return false;
    }

    hal::GpioDirection hdir = (dir == OUTPUT) ? hal::GpioDirection::OUTPUT : hal::GpioDirection::INPUT;
    return m_halGpio && m_halGpio->setDirection(m_gpioNum, hdir);
}

void GPIO::blinkThreadFunc(double frequency, int duration, double dutyCycle)
{
    // Validate parameters
    if (frequency <= 0 || dutyCycle < 0 || dutyCycle > 1) {
        Logger::log(LogLevel::ERROR, "Invalid parameters for async blink");
        std::lock_guard<std::mutex> lock(*m_blinkMutex);
        m_blinkActive = false;
        return;
    }

    // Check if GPIO is exported and set to output
    if (!m_isExported && !exportGPIO()) {
        Logger::log(LogLevel::ERROR, "Failed to export GPIO %d for async blink", m_gpioNum);
        std::lock_guard<std::mutex> lock(*m_blinkMutex);
        m_blinkActive = false;
        return;
    }

    if (!setDirection(OUTPUT)) {
        Logger::log(LogLevel::ERROR, "Failed to set GPIO %d as output for async blink", m_gpioNum);
        std::lock_guard<std::mutex> lock(*m_blinkMutex);
        m_blinkActive = false;
        return;
    }

    // Calculate period in milliseconds
    int periodMs = static_cast<int>(1000.0 / frequency);
    
    // Calculate on and off times based on duty cycle
    int onTimeMs = static_cast<int>(periodMs * dutyCycle);
    // offTimeMs is needed for parameter updates but not used in breathing effect
    int offTimeMs = periodMs - onTimeMs;
    
    // Determine number of cycles
    bool infiniteLoop = (duration <= 0);
    int totalCycles = 0;
    
    if (!infiniteLoop) {
        totalCycles = static_cast<int>((duration / 1000.0) * frequency);
    }
    
    auto startTime = std::chrono::steady_clock::now();
    int currentCycle = 0;
    
    // Breathing LED implementation
    // For true breathing effect, we use sine wave to smoothly transition between bright and dim
    const int breathingSteps = 50;  // Number of steps for smooth transition
    
    while (true) {
        bool shouldContinue = false;
        bool parametersUpdated = false;
        
        // Check if we should continue and update parameters in one locked section
        {
            std::lock_guard<std::mutex> lock(*m_blinkMutex);
            shouldContinue = m_blinkActive && (infiniteLoop || currentCycle < totalCycles);
            
            if (!shouldContinue) break;
            
            if (frequency != m_currentFrequency || 
                duration != m_currentDuration || 
                dutyCycle != m_currentDutyCycle) {
                // Parameters have changed, update them
                frequency = m_currentFrequency;
                duration = m_currentDuration;
                dutyCycle = m_currentDutyCycle;
                
                // Recalculate timings
                periodMs = static_cast<int>(1000.0 / frequency);
                onTimeMs = static_cast<int>(periodMs * dutyCycle);
                offTimeMs = periodMs - onTimeMs; // Update offTimeMs for parameter consistency
                
                infiniteLoop = (duration <= 0);
                if (!infiniteLoop) {
                    totalCycles = static_cast<int>((duration / 1000.0) * frequency);
                    currentCycle = 0;
                    startTime = std::chrono::steady_clock::now();
                }
                parametersUpdated = true;
            }
        }
        
        if (!shouldContinue) break;
        if (parametersUpdated) continue; // Skip this cycle if parameters just changed
        
        // Implement breathing effect using sine wave for smooth transitions
        for (int step = 0; step < breathingSteps; step++) {
            // Check if we should continue in a locked context
            bool shouldContinue = false;
            {
                std::lock_guard<std::mutex> lock(*m_blinkMutex);
                shouldContinue = m_blinkActive;
            }
            
            if (!shouldContinue) break;
            // Calculate brightness using sine wave (0 to 1 to 0)
            double phase = 2.0 * M_PI * step / breathingSteps;
            double brightness = (std::sin(phase) + 1.0) / 2.0;  // Convert to 0-1 range
            
            // Calculate on/off times for this step
            int stepOnTime = static_cast<int>(periodMs * dutyCycle * brightness);
            int stepOffTime = static_cast<int>(periodMs * dutyCycle * (1.0 - brightness));
            
            // Apply the current brightness level
            if (stepOnTime > 0 && m_blinkActive) {
                setValue(HIGH);
                
                // Sleep in small intervals to check if we should stop
                const int sleepInterval = 10; // Check every 10ms
                int remainingSleep = stepOnTime;
                while (remainingSleep > 0 && m_blinkActive) {
                    int sleepTime = std::min(sleepInterval, remainingSleep);
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
                    remainingSleep -= sleepTime;
                }
                
                // Check m_blinkActive again after sleep
                bool shouldContinue = false;
                {
                    std::lock_guard<std::mutex> lock(*m_blinkMutex);
                    shouldContinue = m_blinkActive;
                }
                if (!shouldContinue) break;
            }
            
            if (stepOffTime > 0 && m_blinkActive) {
                setValue(LOW);
                
                // Sleep in small intervals to check if we should stop
                const int sleepInterval = 10; // Check every 10ms
                int remainingSleep = stepOffTime;
                while (remainingSleep > 0 && m_blinkActive) {
                    int sleepTime = std::min(sleepInterval, remainingSleep);
                    std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
                    remainingSleep -= sleepTime;
                }
                
                // Check m_blinkActive after sleep
                bool shouldContinue = false;
                {
                    std::lock_guard<std::mutex> lock(*m_blinkMutex);
                    shouldContinue = m_blinkActive;
                }
                if (!shouldContinue) break;
            }
        }
        
        // Check if we should continue
        shouldContinue = false;
        {
            std::lock_guard<std::mutex> lock(*m_blinkMutex);
            shouldContinue = m_blinkActive;
        }
        
        if (shouldContinue) {
            currentCycle++;
            
            // Check if duration has elapsed
            if (!infiniteLoop) {
                auto currentTime = std::chrono::steady_clock::now();
                auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
                if (elapsedTime >= duration) {
                    break;
                }
            }
        }
    }
    
    // Turn off LED when finished or stopped
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(*m_blinkMutex);
        wasActive = m_blinkActive;
        // If we finished naturally, mark as inactive
        if (wasActive) {
            m_blinkActive = false;
        }
    }
    
    // Turn off LED only if we finished naturally (not explicitly stopped)
    if (wasActive) {
        setValue(LOW);
    }
}

bool GPIO::asyncBlink(double frequency, int duration, double dutyCycle)
{
    // Validate parameters
    if (frequency <= 0 || dutyCycle < 0 || dutyCycle > 1) {
        Logger::log(LogLevel::ERROR, "Invalid parameters for asyncBlink");
        return false;
    }

    // Check if GPIO is exported and set to output
    if (!m_isExported && !exportGPIO()) {
        Logger::log(LogLevel::ERROR, "Failed to export GPIO %d for asyncBlink", m_gpioNum);
        return false;
    }

    if (!setDirection(OUTPUT)) {
        Logger::log(LogLevel::ERROR, "Failed to set GPIO %d as output for asyncBlink", m_gpioNum);
        return false;
    }

    std::lock_guard<std::mutex> lock(*m_blinkMutex);
    
    // Update current parameters
    m_currentFrequency = frequency;
    m_currentDuration = duration;
    m_currentDutyCycle = dutyCycle;
    
    if (m_blinkActive) {
        // If already blinking, just update parameters (the thread will pick them up)
        return true;
    }
    
    // Start a new blink thread
    m_blinkActive = true;
    try {
        m_blinkThread = std::make_shared<std::thread>(&GPIO::blinkThreadFunc, this, frequency, duration, dutyCycle);
        // Do not detach the thread so we can join it later in stopBlink
        // m_blinkThread->detach(); // Detach the thread to run independently
    } catch (const std::system_error& e) {
        Logger::log(LogLevel::ERROR, "Failed to start blink thread: %s", e.what());
        m_blinkActive = false;
        return false;
    }
    
    return true;
}

bool GPIO::stopBlink() {
    std::shared_ptr<std::thread> threadToJoin;
    
    {
        std::lock_guard<std::mutex> lock(*m_blinkMutex);
        if (!m_blinkActive) {
            // Not blinking, nothing to do
            return true;
        }
        
        // Signal the thread to stop
        m_blinkActive = false;
        
        // Store thread pointer for joining outside the lock scope
        threadToJoin = m_blinkThread;
    }
    
    // Wait for the thread to finish if it's joinable, outside the lock scope to avoid deadlock
    if (threadToJoin && threadToJoin->joinable()) {
        try {
            threadToJoin->join();
        } catch (const std::system_error& e) {
            Logger::log(LogLevel::ERROR, "Failed to join blink thread: %s", e.what());
            return false;
        }
    }
    
    return true;
}

bool GPIO::setConstant(GPIO_VALUE value)
{
    // Stop any ongoing blinking
    stopBlink();
    
    // Check if GPIO is exported and set to output
    if (!m_isExported && !exportGPIO()) {
        Logger::log(LogLevel::ERROR, "Failed to export GPIO %d for setConstant", m_gpioNum);
        return false;
    }

    if (!setDirection(OUTPUT)) {
        Logger::log(LogLevel::ERROR, "Failed to set GPIO %d as output for setConstant", m_gpioNum);
        return false;
    }
    
    // Set the constant value
    return setValue(value);
}

bool GPIO::getDirection(GPIO_DIRECTION &dir)
{
    // 检查是否已导出
    if (!m_isExported && !exportGPIO()) {
        return false;
    }

    hal::GpioDirection hdir;
    if (!(m_halGpio && m_halGpio->getDirection(m_gpioNum, hdir))) return false;
    dir = (hdir == hal::GpioDirection::OUTPUT) ? OUTPUT : INPUT;
    return true;
}

bool GPIO::setValue(GPIO_VALUE value)
{
    // 检查是否已导出
    if (!m_isExported && !exportGPIO()) {
        return false;
    }

    hal::GpioValue hval = (value == HIGH) ? hal::GpioValue::HIGH : hal::GpioValue::LOW;
    return m_halGpio && m_halGpio->setValue(m_gpioNum, hval);
}

bool GPIO::getValue(GPIO_VALUE &value)
{
    // 检查是否已导出
    if (!m_isExported && !exportGPIO()) {
        return false;
    }

    hal::GpioValue hval;
    if (!(m_halGpio && m_halGpio->getValue(m_gpioNum, hval))) return false;
    value = (hval == hal::GpioValue::HIGH) ? HIGH : LOW;
    return true;
}

bool GPIO::setEdge(GPIO_EDGE edge)
{
    // 检查是否已导出
    if (!m_isExported && !exportGPIO()) {
        return false;
    }

    hal::GpioEdge hedge = hal::GpioEdge::NONE;
    if (edge == EDGE_RISING) hedge = hal::GpioEdge::RISING;
    else if (edge == EDGE_FALLING) hedge = hal::GpioEdge::FALLING;
    else if (edge == EDGE_BOTH) hedge = hal::GpioEdge::BOTH;
    return m_halGpio && m_halGpio->setEdge(m_gpioNum, hedge);
}

bool GPIO::setActiveLow(bool isActiveLow)
{
    // 检查是否已导出
    if (!m_isExported && !exportGPIO()) {
        return false;
    }

    return m_halGpio && m_halGpio->setActiveLow(m_gpioNum, isActiveLow);
}

bool GPIO::getActiveLow(bool &isActiveLow)
{
    // 检查是否已导出
    if (!m_isExported && !exportGPIO()) {
        return false;
    }

    return m_halGpio && m_halGpio->getActiveLow(m_gpioNum, isActiveLow);
}

int GPIO::getPinNum() const
{
    return m_gpioNum;
}

bool GPIO::isExported() const
{
    return m_isExported;
}

bool GPIO::blink(double frequency, int duration, double dutyCycle)
{
    // This is the synchronous version of blink
    // It blocks the calling thread until the blinking is done or interrupted
    
    // Validate parameters
    if (frequency <= 0 || dutyCycle < 0 || dutyCycle > 1) {
        Logger::log(LogLevel::ERROR, "Invalid parameters for blink");
        return false;
    }

    // Check if GPIO is exported and set to output
    if (!m_isExported && !exportGPIO()) {
        Logger::log(LogLevel::ERROR, "Failed to export GPIO %d for blink", m_gpioNum);
        return false;
    }

    if (!setDirection(OUTPUT)) {
        Logger::log(LogLevel::ERROR, "Failed to set GPIO %d as output for blink", m_gpioNum);
        return false;
    }

    // Calculate period in milliseconds
    int periodMs = static_cast<int>(1000.0 / frequency);
    
    // Calculate on time based on duty cycle (used for parameter validation)
    int onTimeMs = static_cast<int>(periodMs * dutyCycle);
    // offTimeMs is intentionally not used as we're using a breathing effect instead
    // int offTimeMs = periodMs - onTimeMs;
    
    // Determine number of cycles
    bool infiniteLoop = (duration <= 0);
    int totalCycles = 0;
    
    if (!infiniteLoop) {
        totalCycles = static_cast<int>((duration / 1000.0) * frequency);
    }
    
    auto startTime = std::chrono::steady_clock::now();
    int currentCycle = 0;
    
    // Breathing LED implementation
    // For true breathing effect, we use sine wave to smoothly transition between bright and dim
    const int breathingSteps = 50;  // Number of steps for smooth transition
    
    while (infiniteLoop || currentCycle < totalCycles) {
        // Implement breathing effect using sine wave for smooth transitions
        for (int step = 0; step < breathingSteps; step++) {
            // Calculate brightness using sine wave (0 to 1 to 0)
            double phase = 2.0 * M_PI * step / breathingSteps;
            double brightness = (std::sin(phase) + 1.0) / 2.0;  // Convert to 0-1 range
            
            // Calculate on/off times for this step
            int stepOnTime = static_cast<int>(periodMs * dutyCycle * brightness);
            int stepOffTime = static_cast<int>(periodMs * dutyCycle * (1.0 - brightness));
            
            // Apply the current brightness level
            if (stepOnTime > 0) {
                setValue(HIGH);
                std::this_thread::sleep_for(std::chrono::milliseconds(stepOnTime));
            }
            
            if (stepOffTime > 0) {
                setValue(LOW);
                std::this_thread::sleep_for(std::chrono::milliseconds(stepOffTime));
            }
        }
        
        currentCycle++;
        
        // Check if duration has elapsed
        if (!infiniteLoop) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();
            if (elapsedTime >= duration) {
                break;
            }
        }
    }
    
    // Turn off LED when finished
    setValue(LOW);
    
    return true;
}

bool GPIO::writeString(const std::string &path, const std::string &value)
{
    std::ofstream file;
    file.open(path.c_str());
    
    if (!file.is_open()) {
        Logger::log(LogLevel::ERROR, "Failed to open file: %s", path.c_str());
        return false;
    }
    
    file << value;
    file.close();
    
    return true;
}

bool GPIO::readString(const std::string &path, std::string &value)
{
    std::ifstream file;
    file.open(path.c_str());
    
    if (!file.is_open()) {
        Logger::log(LogLevel::ERROR, "Failed to open file: %s", path.c_str());
        return false;
    }
    
    file >> value;
    file.close();
    
    return true;
}
