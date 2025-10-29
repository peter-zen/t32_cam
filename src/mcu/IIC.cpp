#include "IIC.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <vector>
#include "Logger.h"
#include "StringConvert.h"

#if !MCU_EXIST
#define I2C_BYPASS 1
#endif

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

	if (iic_fd >= 0) {
		::close(iic_fd);
		iic_fd = -1;
	}
	return true;
}

int IIC::read(int reg, unsigned char *buf, int len)
{
	#ifdef I2C_BYPASS
	*buf = 0x00;
	return len;
	#endif

	if (iic_fd < 0) {
		Logger::log(LogLevel::ERROR, "IIC bus is not open");
		return -1;
	}

	unsigned char reg_buf[1] = { static_cast<unsigned char>(reg) };
	if (write(iic_fd, reg_buf, 1) != 1) {
		Logger::log(LogLevel::ERROR, "Failed to write register address");
		return -1;
	}

	return ::read(iic_fd, buf, len);
}

int IIC::write(int reg, unsigned char *buf, int len)
{
	#ifdef I2C_BYPASS
	return len;
	#endif

	if (iic_fd < 0) {
		Logger::log(LogLevel::ERROR, "IIC bus is not open");
		return -1;
	}

	std::vector<unsigned char> data(len + 1);
	data[0] = static_cast<unsigned char>(reg);
	std::copy(buf, buf + len, data.begin() + 1);

	return ::write(iic_fd, data.data(), len + 1) == (len + 1) ? len : -1;
}