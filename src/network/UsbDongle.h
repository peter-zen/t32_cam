#ifndef USB4G_DONGLE_H
#define USB4G_DONGLE_H

#include <memory>
#include <string>
#include "SerialPort.h"

namespace network {
enum class UsbDongleModel { EC20, EC200A, EG800K , RG255AA};
class UsbDongle {
public:
	static std::shared_ptr<UsbDongle> getInstance();
	void setModel(const UsbDongleModel &model);
	bool getSimNumber(std::string &sim_number);
	bool getIP(std::string &ip);
	bool ping(const std::string &ip);
	bool getSignal(int &signal);
	int getScanMode();
	bool getAct(int &act);
	bool sleep();
	bool powerOff();
	bool start();
	bool stop();
	bool preconfig();
	bool setSimPin(const std::string &pin);
	bool open(const std::string &device_name="");
	bool close();
	bool loadDriver();
	~UsbDongle();
	
private:
	void clear();
	bool setEcho(bool enable);
	bool setErrMsgFmt(int format);
	bool setDevCtrl(int type, int cid);
	bool setNetScanMode(int scanmode, int effect);
	bool setNetType(int type);
	bool setWakeupConfig();
	bool querySimReady(int &status);
	bool setPinInternal(const std::string &pin);
	bool getApn(std::string &apn);
	bool setContextProfile(const std::string &apn);
	bool activateContextProfile();
	bool deactivateContextProfile();
	bool probe();
	bool echoError(const std::string &resp);
	void echoCMEError(int code);
	void echoCMSError(int code);
	
	// Helper functions for driver loading
	bool have(const std::string& cmd);
	bool loaded(const std::string& module_name);
	bool load_one(const std::string& module_name);
	const char *usb4g_model_name(UsbDongleModel model);
private:
	UsbDongle();
	
	UsbDongle(const UsbDongle &) = delete;
	UsbDongle &operator=(const UsbDongle &) = delete;

    // Error message structures and arrays
    struct ErrorMessage {
        int code;
        std::string message;
    };

    static ErrorMessage cme_error[];
    static ErrorMessage cms_error[];

	UsbDongleModel model;
	SerialPort io;
	std::string simPinCode;
};
}
#endif
