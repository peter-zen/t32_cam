#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <vector>
#include "SerialPort.h"
#include <sys/select.h>

static speed_t to_speed(int baud_rate)
{
	switch (baud_rate) {
		case 1200: return B1200;
		case 2400: return B2400;
		case 4800: return B4800;
		case 9600: return B9600;
		case 19200: return B19200;
		case 38400: return B38400;
		case 57600: return B57600;
		case 115200: return B115200;
#ifdef B230400
		case 230400: return B230400;
#endif
#ifdef B460800
		case 460800: return B460800;
#endif
#ifdef B921600
		case 921600: return B921600;
#endif
		default: return 0;
	}
}
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
	this->device_name = device_name;
	this->baud_rate = baud_rate;
	fd = ::open(device_name.c_str(), O_RDWR | O_NOCTTY);
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

	speed_t speed = to_speed(baud_rate);
	if (speed == 0) {
		std::cerr << "Unsupported baud rate: " << baud_rate << std::endl;
		return false;
	}

	cfmakeraw(&options);
	cfsetispeed(&options, speed);
	cfsetospeed(&options, speed);

	options.c_cflag |= (CLOCAL | CREAD);
	options.c_cflag &= ~PARENB;
	options.c_cflag &= ~CSTOPB;
	options.c_cflag &= ~CSIZE;
	options.c_cflag |= CS8;
#ifdef CRTSCTS
	options.c_cflag &= ~CRTSCTS;
#endif
	options.c_iflag &= ~(IXON | IXOFF | IXANY);
	options.c_cc[VMIN] = 1;
	options.c_cc[VTIME] = 0;

	if (tcsetattr(fd, TCSANOW, &options) != 0) {
		std::cerr << "Failed to set attributes for " << device_name << std::endl;
		return false;
	}

	tcflush(fd, TCIOFLUSH);
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

bool SerialPort::read(std::string &data, int timeout)
{
	std::lock_guard<std::mutex> lock(io_mutex);
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
		char buffer[512];
		ssize_t bytes_read = ::read(fd, buffer, sizeof(buffer));
		if (bytes_read > 0) {
			data.assign(buffer, bytes_read);
			return true;
		}
		return false;
	}
	return false;
}
