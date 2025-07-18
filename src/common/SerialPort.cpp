#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <vector>
#include "SerialPort.h"
#include <sys/select.h>
SerialPort::SerialPort()
	: fd(-1)
{
}

SerialPort::~SerialPort()
{
	close();
}

bool SerialPort::open(const std::string &device_name, int baud_rate)
{
	std::lock_guard<std::mutex> lock(io_mutex);
	fd = ::open(device_name.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
	if (fd == -1) {
		std::cerr << "Failed to open " << device_name << std::endl;
		return false;
	}
	return configure();
}

bool SerialPort::close()
{
	std::lock_guard<std::mutex> lock(io_mutex);
	if (fd != -1) {
		::close(fd);
		fd = -1;
	}

        return true;
}

bool SerialPort::configure()
{
	struct termios options;
	if (tcgetattr(fd, &options) != 0) {
		std::cerr << "Failed to get attributes for " << device_name << std::endl;
		return false;
	}

	cfsetispeed(&options, baud_rate);
	cfsetospeed(&options, baud_rate);

	options.c_cflag |= (CLOCAL | CREAD);
	options.c_cflag &= ~PARENB;
	options.c_cflag &= ~CSTOPB;
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= CS8;

	if (tcsetattr(fd, TCSANOW, &options) != 0) {
		std::cerr << "Failed to set attributes for " << device_name << std::endl;
		return false;
	}

	return true;
}

bool SerialPort::write(const std::string &data)
{
	std::lock_guard<std::mutex> lock(io_mutex);
	if (fd == -1) {
		std::cerr << "Device not open" << std::endl;
		return false;
	}

	ssize_t bytes_written = ::write(fd, data.c_str(), data.size());
	return bytes_written == static_cast<ssize_t>(data.size());
}

bool SerialPort::read(std::string &data)
{
	if (fd == -1) {
		std::cerr << "Device not open" << std::endl;
		return false;
	}

	char buffer[512];
	ssize_t bytes_read = ::read(fd, buffer, sizeof(buffer));
	if (bytes_read > 0) {
		data.assign(buffer, bytes_read);
		return true;
	}
	return false;
}

bool SerialPort::read(std::string &data, int timeout)
{
	if (fd == -1) {
		std::cerr << "Device not open" << std::endl;
		return false;
	}

	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(fd, &read_fds);

	struct timeval tv;
	tv.tv_sec = timeout / 1000;
	tv.tv_usec = (timeout % 1000) * 1000;

	int result = select(fd + 1, &read_fds, NULL, NULL, &tv);
	if (result > 0) {
		return read(data);
	}
	return false;
}
