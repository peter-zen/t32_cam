#pragma once
#include "IGpio.h"
#include <string>
namespace hal {
class IngenicGpio : public IGpio {
public:
    bool exportPin(int pin) override;
    bool unexportPin(int pin) override;
    bool setDirection(int pin, GpioDirection dir) override;
    bool getDirection(int pin, GpioDirection& dir) override;
    bool setValue(int pin, GpioValue val) override;
    bool getValue(int pin, GpioValue& val) override;
    bool setEdge(int pin, GpioEdge edge) override;
    bool setActiveLow(int pin, bool activeLow) override;
    bool getActiveLow(int pin, bool& activeLow) override;
private:
    static std::string gpioPath(int pin);
    static bool writeString(const std::string& path, const std::string& value);
    static bool readString(const std::string& path, std::string& value);
};
}
