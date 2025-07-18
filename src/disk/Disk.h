#ifndef DISK_H
#define DISK_H

#include <stdint.h>
#include <string>

struct DiskInfo {
	int total;
	int free;
};

class Disk {
    public:
	static DiskInfo getInfo(const std::string &path = "/");

    private:
	Disk() = default;
	~Disk() = default;
	Disk(const Disk &) = delete;
	Disk &operator=(const Disk &) = delete;
};

#endif