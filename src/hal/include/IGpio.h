#pragma once
#include <memory>
namespace hal {

enum class GpioDirection {
    INPUT = 0,
    OUTPUT = 1
};

enum class GpioValue {
    LOW = 0,
    HIGH = 1
};

enum class GpioEdge {
    NONE = 0,
    RISING = 1,
    FALLING = 2,
    BOTH = 3
};

class IGpio {
public:
    virtual ~IGpio() {}
    virtual bool exportPin(int pin) = 0;
    virtual bool unexportPin(int pin) = 0;
    virtual bool setDirection(int pin, GpioDirection dir) = 0;
    virtual bool getDirection(int pin, GpioDirection& dir) = 0;
    virtual bool setValue(int pin, GpioValue val) = 0;
    virtual bool getValue(int pin, GpioValue& val) = 0;
    virtual bool setEdge(int pin, GpioEdge edge) = 0;
    virtual bool setActiveLow(int pin, bool activeLow) = 0;
    virtual bool getActiveLow(int pin, bool& activeLow) = 0;
};

}
