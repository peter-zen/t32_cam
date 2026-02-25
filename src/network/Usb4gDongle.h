#ifndef USB4G_DONGLE_H
#define USB4G_DONGLE_H

#include <memory>
#include <string>
#include "utils/serial/SerialPort.h"

namespace network {
enum class Usb4gDongleModel { EC20, EC200 };
class Usb4gDongle {
    public:
	static std::shared_ptr<Usb4gDongle> getInstance();
	void setModel(const Usb4gDongleModel &model);
	bool getSimNumber(std::string &sim_number);
	bool open();
	bool close();
	~Usb4gDongle();
	
    private:
	void clear();
	bool setEcho(bool enable);
	bool setErrMsgFmt(int format);
	bool setDevCtrl(int type, int cid);
	bool setNetScanMode(int scanmode, int effect);
	bool setNetType(int type);
	bool setWakeupConfig();

    private:
	Usb4gDongle();
	
	Usb4gDongle(const Usb4gDongle &) = delete;
	Usb4gDongle &operator=(const Usb4gDongle &) = delete;

    private:
	Usb4gDongleModel model;
	SerialPort io;
};
}
#endif
