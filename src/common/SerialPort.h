#ifndef SERIAL_PORT_H
#define SERIAL_PORT_H

#include <mutex>
#include <string>

class SerialPort {
    public:
	bool open(const std::string &device_name, int baud_rate);
	bool close();
	bool write(const std::string &data);
	bool read(std::string &data);
	bool read(std::string &data, int timeout);

    public:
	SerialPort();
	~SerialPort();

    private:
	bool configure();

    private:
	std::string device_name;
	int baud_rate;
	int fd;
	std::mutex io_mutex;
};

#endif
