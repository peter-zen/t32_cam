#include <mutex>
#include <csignal>
#include <unistd.h>
#include <cstdlib>
#include <sys/types.h>
#include "DayNightSwitch.h"
#include "Logger.h"
#include "GPIO.h"
#include "HalFactory.h"
#include "IVideo.h"
#include "Common.h"
#include "DeviceConfig.h"

std::shared_ptr<DayNightSwitch> DayNightSwitch::getInstance()
{
    static std::shared_ptr<DayNightSwitch> instance = nullptr;
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new DayNightSwitch()); });
    return instance;
}

DayNightSwitch::DayNightSwitch()
{
    this->cdsPin = PA(10);
    this->irledPin = PB(10);
    this->irCutEnablePin = PB(13);
    this->irCutCtrlPin = PB(14);
    this->dayNightState = DayNightState::DAY;
    this->autoSwitchThreadRunning = false;
    this->autoSwitchThreadSuspended = false;
    this->autoSwitchThread = nullptr;
}

DayNightSwitch::~DayNightSwitch()
{
    if (this->autoSwitchThread != nullptr) {
        stopAutoSwitch();
    }
}

void DayNightSwitch::setCdsPins(int pin)
{
    this->cdsPin = pin;
}

void DayNightSwitch::setIRLedPins(int pin)
{
    this->irledPin = pin;
}

void DayNightSwitch::setIRCutPins(int enable_pin, int ctrl_pin)
{
    this->irCutEnablePin = enable_pin;
    this->irCutCtrlPin = ctrl_pin;
}

enum DayNightState DayNightSwitch::getDayNightState()
{
    GPIO_VALUE value_cds;

    auto gpio_cds = GPIO(this->cdsPin);
    if (!gpio_cds.exportGPIO()) {
        Logger::log(LogLevel::ERROR, "export gpio(%d) failed", this->cdsPin);
        this->dayNightState = DayNightState::MAX;
        goto func_exit;
    }

    if (!gpio_cds.setDirection(GPIO_DIRECTION::INPUT)) {
        Logger::log(LogLevel::ERROR, "set gpio(%d) direction input failed", this->cdsPin);
        this->dayNightState = DayNightState::MAX;
        goto func_exit;
    }
    //gpio_cds.setActiveLow(true);
    if (!gpio_cds.getValue(value_cds)) {
        Logger::log(LogLevel::ERROR, "get gpio(%d) value failed", this->cdsPin);
        this->dayNightState = DayNightState::MAX;
        goto func_exit;
    }

    if (value_cds == GPIO_VALUE::HIGH) {
        this->dayNightState = DayNightState::NIGHT;
    } else {
        this->dayNightState = DayNightState::DAY;
    }

func_exit:
    return this->dayNightState;
}

bool DayNightSwitch::controlIRCut(DayNightState state)
{

    if (state == DayNightState::MAX) {
        Logger::log(LogLevel::ERROR, "DayNightState is MAX");
        return false;
    }

    auto wled = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_WLED, 0);
    if (state == DayNightState::NIGHT && wled == 1) {
        state = DayNightState::DAY;
    }

    auto gpio_ircut_enable_pin = GPIO(irCutEnablePin);
    auto gpio_ircut_ctrl_pin = GPIO(irCutCtrlPin);

    if (!gpio_ircut_enable_pin.exportGPIO() || !gpio_ircut_ctrl_pin.exportGPIO()) {
        Logger::log(LogLevel::ERROR, "export gpio(%d, %d) failed", irCutEnablePin, irCutCtrlPin);
        return false;
    }

    if (!gpio_ircut_enable_pin.setDirection(GPIO_DIRECTION::OUTPUT) || !gpio_ircut_ctrl_pin.setDirection(GPIO_DIRECTION::OUTPUT)) {
        Logger::log(LogLevel::ERROR, "set gpio(%d, %d) direction output failed", irCutEnablePin, irCutCtrlPin);
        return false;
    }

    if (state == DayNightState::DAY) {
        gpio_ircut_ctrl_pin.setValue(GPIO_VALUE::HIGH); 
        gpio_ircut_enable_pin.setValue(GPIO_VALUE::LOW);
        usleep(100000);//100ms
        gpio_ircut_enable_pin.setValue(GPIO_VALUE::HIGH);
        gpio_ircut_ctrl_pin.setValue(GPIO_VALUE::LOW);
    } else {
        gpio_ircut_ctrl_pin.setValue(GPIO_VALUE::LOW); 
        gpio_ircut_enable_pin.setValue(GPIO_VALUE::LOW);
        usleep(100000);//100ms
        gpio_ircut_enable_pin.setValue(GPIO_VALUE::HIGH);
        gpio_ircut_ctrl_pin.setValue(GPIO_VALUE::LOW);
    }

    return true;
}

bool DayNightSwitch::controlIRLed(DayNightState state)
{
    if (state == DayNightState::MAX) {
        Logger::log(LogLevel::ERROR, "DayNightState is MAX");
        return false;
    }

    auto gpio_irled = GPIO(irledPin);

    if (!gpio_irled.exportGPIO()) {
        Logger::log(LogLevel::ERROR, "export gpio(%d) failed", irledPin);
        return false;
    }

    if (!gpio_irled.setDirection(GPIO_DIRECTION::OUTPUT)) {
        Logger::log(LogLevel::ERROR, "set gpio(%d) direction output failed", irledPin);
        return false;
    }

    if (state == DayNightState::DAY) {
        gpio_irled.setValue(GPIO_VALUE::LOW);
    } else {
        gpio_irled.setValue(GPIO_VALUE::HIGH);
    }

    return true;
}

bool DayNightSwitch::controlISP(DayNightState state)
{
    if (state == DayNightState::MAX) {
        Logger::log(LogLevel::ERROR, "DayNightState is MAX");
        return false;
    }
 
    auto wled = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_WLED, 0);
    if (state == DayNightState::NIGHT && wled == 1) {
        state = DayNightState::DAY;
    }
 
    auto cfg = hal::HalFactory::createVideoControl();
    if (!cfg) return false;
    hal::ISPDaynightMode current;
    if (!cfg->getISPMode(current)) return false;
    hal::ISPDaynightMode target = (state == DayNightState::DAY) ? hal::ISPDaynightMode::DAY : hal::ISPDaynightMode::NIGHT;
    if (current != target) {
        if (!cfg->setISPMode(target)) return false;
    }
    return true;
}

bool DayNightSwitch::startAutoSwithch()
{
    // If thread is already running, just resume it
    if (this->autoSwitchThread != nullptr) {
        return resumeAutoSwitch();
    }

    this->autoSwitchThreadRunning = true;
    this->autoSwitchThreadSuspended = false;
    
    // Create new thread
    this->autoSwitchThread = std::make_shared<std::thread>([this]() {
        while (this->autoSwitchThreadRunning) {
            // Check if thread is suspended
            {   // Lock scope
                std::unique_lock<std::mutex> lock(this->threadMutex);
                // Wait until resumed or stopped
                this->threadCV.wait(lock, [this] {
                    return !this->autoSwitchThreadSuspended || !this->autoSwitchThreadRunning;
                });
            }
            
            // If thread is marked to stop, exit loop
            if (!this->autoSwitchThreadRunning) {
                break;
            }
            
            // Execute the day/night switching logic
            this->getDayNightState();
            this->controlIRCut(this->dayNightState);
            this->controlIRLed(this->dayNightState);
            this->controlISP(this->dayNightState);
            
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    });
    
    return this->autoSwitchThread != nullptr;
}

bool DayNightSwitch::stopAutoSwitch()
{
    if (this->autoSwitchThread == nullptr) {
        Logger::log(LogLevel::ERROR, "auto switch thread is not running");
        return true;
    }

    // Mark thread to stop
    {   // Lock scope
        std::lock_guard<std::mutex> lock(this->threadMutex);
        this->autoSwitchThreadRunning = false;
        this->autoSwitchThreadSuspended = false; // Ensure it's not suspended
    }
    
    // Notify the thread in case it's waiting
    this->threadCV.notify_one();
    
    // Wait for thread to complete
    if (this->autoSwitchThread->joinable()) {
        this->autoSwitchThread->join();
    }

    // Reset thread pointer
    this->autoSwitchThread = nullptr;

    return true;
}

bool DayNightSwitch::suspendAutoSwitch()
{
    if (this->autoSwitchThread == nullptr) {
        Logger::log(LogLevel::ERROR, "auto switch thread is not running");
        return false;
    }

    // Mark thread as suspended
    {   // Lock scope
        std::lock_guard<std::mutex> lock(this->threadMutex);
        this->autoSwitchThreadSuspended = true;
    }
    
    return true;
}

bool DayNightSwitch::resumeAutoSwitch()
{
    if (this->autoSwitchThread == nullptr) {
        Logger::log(LogLevel::ERROR, "auto switch thread is not running");
        return false;
    }

    // Mark thread as resumed
    {   // Lock scope
        std::lock_guard<std::mutex> lock(this->threadMutex);
        this->autoSwitchThreadSuspended = false;
    }
    
    // Notify the thread to wake up
    this->threadCV.notify_one();
    
    return true;
}
