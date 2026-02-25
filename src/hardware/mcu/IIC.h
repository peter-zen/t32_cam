#ifndef I2C_H
#define I2C_H

#include <string>
#include <mutex>

class IIC {
public:
	IIC(const std::string &devname);
	~IIC();

public:
	bool open();
	bool close();
	int read(int reg, void *buf, size_t count);
	int write(int reg, void *buf, size_t count);

private:
	std::string device_name;
	int iic_fd;
	std::mutex iic_mutex; // 添加互斥锁保护共享资源
};

#endif
