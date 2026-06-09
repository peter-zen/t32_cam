#include "IIC.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <vector>
#include <mutex>
#include "Logger.h"
#include "StringConvert.h"

#if !MCU_EXIST
#define I2C_BYPASS 1
#endif

struct iic_buf {
	int reg;
	void *data;
	size_t count;
};

IIC::IIC(const std::string &devname)
	: device_name(devname)
	, iic_fd(-1)
{
}

IIC::~IIC()
{
	close();
}

bool IIC::open()
{
	#ifdef I2C_BYPASS
	return true;
	#endif

	std::lock_guard<std::mutex> lock(iic_mutex); // 添加锁保护
	iic_fd = ::open(device_name.c_str(), O_RDWR);
	if (iic_fd < 0) {
		Logger::log(LogLevel::ERROR, "Failed to open the iic bus");
		return false;
	}

	return true;
}

bool IIC::close()
{
	#ifdef I2C_BYPASS
	return true;
	#endif

	std::lock_guard<std::mutex> lock(iic_mutex); // 添加锁保护
	if (iic_fd >= 0) {
		::close(iic_fd);
		iic_fd = -1;
	}
	return true;
}

int IIC::read(int reg, void *buf, size_t count)
{
	#ifdef I2C_BYPASS
	*buf = 0x00;
	return count;
	#endif

	std::lock_guard<std::mutex> lock(iic_mutex); // 添加锁保护
	if (iic_fd < 0) {
		Logger::log(LogLevel::ERROR, "IIC bus is not open");
		return -1;
	}
	
	// 使用与内核驱动匹配的iic_buf结构体进行通信
	struct iic_buf iic_buff = { reg, buf, count };
	return ::read(iic_fd, &iic_buff, sizeof(iic_buff));
}

int IIC::write(int reg, void *buf, size_t count)
{
	#ifdef I2C_BYPASS
	return count;
	#endif

	std::lock_guard<std::mutex> lock(iic_mutex); // 添加锁保护
	if (iic_fd < 0) {
		Logger::log(LogLevel::ERROR, "IIC bus is not open");
		return -1;
	}
	
	// 使用与内核驱动匹配的iic_buf结构体进行通信
	struct iic_buf iic_buff = { reg, buf, count };
	return ::write(iic_fd, &iic_buff, sizeof(iic_buff));
}