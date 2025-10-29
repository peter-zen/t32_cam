#ifndef MCU_H
#define MCU_H
#include <string>
#include <memory>
#include "IIC.h"
#include "MCUParams.h"

class MCU {
public:
	static std::shared_ptr<MCU> getInstance();
	int readWorkingMode();
	std::string readFirmwareVersion();
	bool powerEnoughForFirmwareUpdate();
	bool waitFor(int seconds);
	bool IsWifiStationReady();
	std::string readGps();
	bool writeGps(const std::string &gps);
	int readBatteryVoltage();
	int readExternalVoltage();
	int readShutdownVoltage();
	int readLowPowerVoltage();
	int readBatteryLevel();
	int readBatteryType();
	int readTemperature();
	int readSignalCF();
	int readSignalRSSI();
	int readSignalRSRP();
	int readSignalRSRQ();
	int readSignalSNR();
	int readSignalTD();
	int readSignalTP();
	bool Is4gExist();
	bool writeRemoteWakeup(int remote_wakeup);
	bool setDatetime(const struct tm &time);
	bool IsRemoteWakeup();
	struct tm getDatetime();
	bool useGpsTime();
	int readCds();
	int readRMID();
	int readRMType();
	int readRMValue();
	int readRMBatteryValue();
	int readRMBattery1Value();
	int readRMBattery2Value();
	int readRMSunPowerValue();
	int readRMCount();
	int readEventType();
	int readEventID();
	int readEventNum();

	//新实现
	std::string readVersion();
	std::string readPID();
	std::string readUPID();
	std::string readUPWD();
	bool writePID(const std::string &pid);
	bool writeUPID(const std::string &upid);
	bool writeUPWD(const std::string &password);

	
	~MCU();
	
private:
	MCU();
	
	MCU(const MCU &) = delete;
	MCU &operator=(const MCU &) = delete;

private:
	std::string firmware_version;
	std::string product_name;
	bool power_enough;
	std::shared_ptr<IIC> iic;
};

#endif
