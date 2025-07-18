#include "Settings.h"
#include <mutex>

std::shared_ptr<Settings> Settings::getInstance()
{
	static std::shared_ptr<Settings> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new Settings()); });
	return instance;
}
