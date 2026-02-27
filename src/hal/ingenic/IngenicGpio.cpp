#include "IngenicGpio.h"
#include <sys/stat.h>
#include <fstream>
#include "../../common/utils/string/StringConvert.h"
namespace hal {
static const char* SYSFS_GPIO_PATH = "/sys/class/gpio";
std::string IngenicGpio::gpioPath(int pin) { return std::string(SYSFS_GPIO_PATH) + "/gpio" + to_string_custom(pin); }
bool IngenicGpio::writeString(const std::string& path, const std::string& value) {
    std::ofstream f(path.c_str());
    if (!f.is_open()) return false;
    f << value;
    return true;
}
bool IngenicGpio::readString(const std::string& path, std::string& value) {
    std::ifstream f(path.c_str());
    if (!f.is_open()) return false;
    f >> value;
    return true;
}
bool IngenicGpio::exportPin(int pin) {
    struct stat st;
    std::string path = gpioPath(pin);
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return true;
    return writeString(std::string(SYSFS_GPIO_PATH) + "/export", to_string_custom(pin));
}
bool IngenicGpio::unexportPin(int pin) {
    return writeString(std::string(SYSFS_GPIO_PATH) + "/unexport", to_string_custom(pin));
}
bool IngenicGpio::setDirection(int pin, GpioDirection dir) {
    std::string p = gpioPath(pin) + "/direction";
    return writeString(p, dir == GpioDirection::INPUT ? "in" : "out");
}
bool IngenicGpio::getDirection(int pin, GpioDirection& dir) {
    std::string p = gpioPath(pin) + "/direction";
    std::string v;
    if (!readString(p, v)) return false;
    dir = (v == "in" || v == "INPUT") ? GpioDirection::INPUT : GpioDirection::OUTPUT;
    return true;
}
bool IngenicGpio::setValue(int pin, GpioValue val) {
    std::string p = gpioPath(pin) + "/value";
    return writeString(p, val == GpioValue::HIGH ? "1" : "0");
}
bool IngenicGpio::getValue(int pin, GpioValue& val) {
    std::string p = gpioPath(pin) + "/value";
    std::string v;
    if (!readString(p, v)) return false;
    val = (v == "1" || v == "HIGH") ? GpioValue::HIGH : GpioValue::LOW;
    return true;
}
bool IngenicGpio::setEdge(int pin, GpioEdge edge) {
    std::string p = gpioPath(pin) + "/edge";
    std::string s = "none";
    if (edge == GpioEdge::RISING) s = "rising";
    else if (edge == GpioEdge::FALLING) s = "falling";
    else if (edge == GpioEdge::BOTH) s = "both";
    return writeString(p, s);
}
bool IngenicGpio::setActiveLow(int pin, bool activeLow) {
    std::string p = gpioPath(pin) + "/active_low";
    return writeString(p, activeLow ? "1" : "0");
}
bool IngenicGpio::getActiveLow(int pin, bool& activeLow) {
    std::string p = gpioPath(pin) + "/active_low";
    std::string v;
    if (!readString(p, v)) return false;
    activeLow = (v == "1");
    return true;
}
}
