#include "WorkMode.h"
#include "Logger.h"
#include "GPIO.h"

enum workingMode WorkMode::working_mode = WORKING_MODE_SNAP_ONLY;
bool WorkMode::already_get_mode = false;
int WorkMode::mode_pin_0 = PC(9);
int WorkMode::mode_pin_1 = PC(8);

void WorkMode::setWorkingModePins(int pin0, int pin1)
{
    mode_pin_0 = pin0;
    mode_pin_1 = pin1;
}

enum workingMode WorkMode::getWorkingMode()
{
    if (!already_get_mode) {
        GPIO_VALUE value_mode_pin_0, value_mode_pin_1;

        auto gpio_mode_pin_0 = GPIO(mode_pin_0);
        auto gpio_mode_pin_1 = GPIO(mode_pin_1);

        if (!gpio_mode_pin_0.exportGPIO() || !gpio_mode_pin_1.exportGPIO()) {
            Logger::log(LogLevel::ERROR, "export gpio(%d, %d) failed", mode_pin_0, mode_pin_1);
            working_mode = workingMode::WORKING_MODE_MAX;
            return working_mode;
        }

        if (!gpio_mode_pin_0.setDirection(GPIO_DIRECTION::INPUT) || !gpio_mode_pin_1.setDirection(GPIO_DIRECTION::INPUT)) {
            Logger::log(LogLevel::ERROR, "set gpio(%d, %d) direction input failed", mode_pin_0, mode_pin_1);
            working_mode = workingMode::WORKING_MODE_MAX;
            return working_mode;
        }

        if (!gpio_mode_pin_0.getValue(value_mode_pin_0) || !gpio_mode_pin_1.getValue(value_mode_pin_1)) {
            Logger::log(LogLevel::ERROR, "get gpio(%d, %d) value failed", mode_pin_0, mode_pin_1);
            working_mode = workingMode::WORKING_MODE_MAX;
            return working_mode;
        }

        if (value_mode_pin_0 == GPIO_VALUE::LOW && value_mode_pin_1 == GPIO_VALUE::LOW) {
            working_mode = workingMode::WORKING_MODE_SNAP_ONLY;
            already_get_mode = true;
            return working_mode;
        } else if (value_mode_pin_0 == GPIO_VALUE::LOW && value_mode_pin_1 == GPIO_VALUE::HIGH) {
            already_get_mode = true;
            working_mode = workingMode::WORKING_MODE_SNAP_UPLOAD;
            return working_mode;
        } else if (value_mode_pin_0 == GPIO_VALUE::HIGH && value_mode_pin_1 == GPIO_VALUE::LOW) {
            working_mode = workingMode::WORKING_MODE_UPLOAD_ONLY;
            already_get_mode = true;
            return working_mode;
        }else {
            working_mode = workingMode::WORKING_MODE_TEST_ONLY;
            already_get_mode = true;
            return working_mode;
        }
    }

    return working_mode;
}

bool WorkMode::setWorkingMode(enum workingMode mode)
{
    auto gpio_mode_pin_0 = GPIO(mode_pin_0);
    auto gpio_mode_pin_1 = GPIO(mode_pin_1);

    if (!gpio_mode_pin_0.exportGPIO() || !gpio_mode_pin_1.exportGPIO()) {
        Logger::log(LogLevel::ERROR, "export gpio(%d, %d) failed", mode_pin_0, mode_pin_1);
        return false;
    }

    if (!gpio_mode_pin_0.setDirection(GPIO_DIRECTION::OUTPUT) || !gpio_mode_pin_1.setDirection(GPIO_DIRECTION::OUTPUT)) {
        Logger::log(LogLevel::ERROR, "set gpio(%d, %d) direction output failed", mode_pin_0, mode_pin_1);
        return false;
    }
    
    switch (working_mode) {
        case workingMode::WORKING_MODE_SNAP_ONLY:
            gpio_mode_pin_0.setValue(GPIO_VALUE::LOW);
            gpio_mode_pin_1.setValue(GPIO_VALUE::LOW);
            break;
        case workingMode::WORKING_MODE_UPLOAD_ONLY:
            gpio_mode_pin_0.setValue(GPIO_VALUE::HIGH);
            gpio_mode_pin_1.setValue(GPIO_VALUE::LOW);
            break;
        case workingMode::WORKING_MODE_TEST_ONLY:
            gpio_mode_pin_0.setValue(GPIO_VALUE::HIGH);
            gpio_mode_pin_1.setValue(GPIO_VALUE::HIGH);
            break;
        case workingMode::WORKING_MODE_SNAP_UPLOAD:
            gpio_mode_pin_0.setValue(GPIO_VALUE::LOW);
            gpio_mode_pin_1.setValue(GPIO_VALUE::HIGH);
            break;
        default:
            return false;
    }

    return true;
}