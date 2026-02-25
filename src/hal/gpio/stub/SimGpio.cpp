#include "SimGpio.h"
namespace hal {
bool SimGpio::exportPin(int pin) { m_[pin].exported = true; return true; }
bool SimGpio::unexportPin(int pin) { m_[pin].exported = false; return true; }
bool SimGpio::setDirection(int pin, GpioDirection dir) { m_[pin].dir = dir; return true; }
bool SimGpio::getDirection(int pin, GpioDirection& dir) { dir = m_[pin].dir; return true; }
bool SimGpio::setValue(int pin, GpioValue val) { m_[pin].val = val; return true; }
bool SimGpio::getValue(int pin, GpioValue& val) { val = m_[pin].val; return true; }
bool SimGpio::setEdge(int pin, GpioEdge edge) { m_[pin].edge = edge; return true; }
bool SimGpio::setActiveLow(int pin, bool activeLow) { m_[pin].activeLow = activeLow; return true; }
bool SimGpio::getActiveLow(int pin, bool& activeLow) { activeLow = m_[pin].activeLow; return true; }
}
