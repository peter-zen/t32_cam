#include "Power.h"
#include <memory>  // 包含 std::shared_ptr 所需头文件
#include <mutex>   // 包含 std::call_once 和 std::once_flag 所需头文件

std::shared_ptr<Power> Power::getInstance()
{
    static std::shared_ptr<Power> instance = nullptr;
    static std::once_flag flag;
    std::call_once(flag, []() { instance.reset(new Power()); });
    return instance;
}

Power::Power()
{
}

Power::~Power()
{
}

void Power::requestShutdown()
{
    //TODO: shutdown
}
