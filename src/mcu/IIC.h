#ifndef I2C_H
#define I2C_H

#include <string>

class IIC {
public:
	IIC(const std::string &devname);
	~IIC();

public:
	bool open();
	bool close();
	int read(int reg, unsigned char *buf, int len);
	int write(int reg, unsigned char *buf, int len);

private:
	std::string device_name;
	int iic_fd;
};

#endif
