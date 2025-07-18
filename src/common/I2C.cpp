#include "I2C.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <iostream>
#include <vector>
#include "Logger.h"
#include "StringConvert.h"

#define I2C_BYPASS 1

I2C::I2C(const std::string &i2c_master, const std::string &i2c_slave)
	: i2c_master(i2c_master)
	, i2c_slave(i2c_slave)
	, i2c_fd(-1)
{
}

I2C::~I2C()
{
	close();
}

bool I2C::open()
{
	#ifdef I2C_BYPASS
	return true;
	#endif

	i2c_fd = ::open(i2c_master.c_str(), O_RDWR);
	if (i2c_fd < 0) {
		Logger::log(LogLevel::ERROR, "Failed to open the i2c bus");
		return false;
	}

	int addr = stoi_custom(i2c_slave, nullptr, 16);
	if (ioctl(i2c_fd, I2C_SLAVE, addr) < 0) {
		Logger::log(LogLevel::ERROR, "Failed to acquire bus access and/or talk to slave");
		::close(i2c_fd);
		i2c_fd = -1;
		return false;
	}

	return true;
}

bool I2C::close()
{
	#ifdef I2C_BYPASS
	return true;
	#endif

	if (i2c_fd >= 0) {
		::close(i2c_fd);
		i2c_fd = -1;
	}
	return true;
}

int I2C::read(int reg, unsigned char *buf, int len)
{
	#ifdef I2C_BYPASS
	*buf = 0x00;
	return len;
	#endif

	if (i2c_fd < 0) {
		Logger::log(LogLevel::ERROR, "I2C bus is not open");
		return -1;
	}

	unsigned char reg_buf[1] = { static_cast<unsigned char>(reg) };
	if (write(i2c_fd, reg_buf, 1) != 1) {
		Logger::log(LogLevel::ERROR, "Failed to write register address");
		return -1;
	}

	return ::read(i2c_fd, buf, len);
}

int I2C::write(int reg, unsigned char *buf, int len)
{
	#ifdef I2C_BYPASS
	return len;
	#endif

	if (i2c_fd < 0) {
		Logger::log(LogLevel::ERROR, "I2C bus is not open");
		return -1;
	}

	std::vector<unsigned char> data(len + 1);
	data[0] = static_cast<unsigned char>(reg);
	std::copy(buf, buf + len, data.begin() + 1);

	return ::write(i2c_fd, data.data(), len + 1) == (len + 1) ? len : -1;
}