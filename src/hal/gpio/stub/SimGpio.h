#pragma once
#include "IGpio.h"
#include <unordered_map>
namespace hal {

struct SimGpioState {
    bool exported = false;
    GpioDirection dir = GpioDirection::OUTPUT;
    GpioValue val = GpioValue::LOW;
    GpioEdge edge = GpioEdge::NONE;
    bool activeLow = false;
};

class SimGpio : public IGpio {
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
    std::unordered_map<int, SimGpioState> m_;
};
}
