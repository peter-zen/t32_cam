#ifndef I2C_H
#define I2C_H

#include <string>

class I2C {
    public:
	I2C(const std::string &i2c_master, const std::string &i2c_slave);
	~I2C();

    public:
	bool open();
	bool close();
	int read(int reg, unsigned char *buf, int len);
	int write(int reg, unsigned char *buf, int len);

    private:
	std::string i2c_master;
	std::string i2c_slave;
	int i2c_fd;
};

#endif
