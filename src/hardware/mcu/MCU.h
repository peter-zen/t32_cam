#ifndef MCU_H
#define MCU_H
#include <string>
#include <memory>
#include "IIC.h"
#include "MCUParams.h"

class MCU {
public:
	static std::shared_ptr<MCU> getInstance();
	std::string readFirmwareVersion();
	bool powerEnoughForFirmwareUpdate();
	bool waitFor(int seconds);
	bool IsWifiStationReady();
	int readShutdownVoltage();
	int readLowPowerVoltage();
	int readBatteryLevel();
	int readBatteryType();
	int readSignalCF();
	int readSignalRSSI();
	int readSignalRSRP();
	int readSignalRSRQ();
	int readSignalSNR();
	int readSignalTD();
	int readSignalTP();
	bool Is4gExist();
	bool writeRemoteWakeup(int remote_wakeup);
	
	bool IsRemoteWakeup();
	bool useGpsTime();
	int readCds();
	int readRMID();
	int readRMType();
	int readRMValue();
	
	int readRMCount();
	int readEventType();
	int readEventID();
	int readEventNum();

	//新实现
	int readWorkingMode();
	std::string convertVersion(int ver);
	int readVersion();
	std::string readPID();
	std::string readUPID();
	std::string readUPWD();
	bool writePID(const std::string &pid);
	bool writeUPID(const std::string &upid);
	bool writeUPWD(const std::string &password);
	bool setDatetime(const struct tm *time);
	struct tm getDatetime();
	int readTemperature();
	int readHumidity();
	int readAtmosPressure();//Atmopheric Pressure
	int readRMSunPowerValue();
	int readExternalVoltage();
	int readBatteryVoltage();
	int readBattery1Voltage();
	int readBattery2Voltage();
	std::string convertVoltage(int value);
	std::string readGps();
	bool writeGps(const std::string &gps);
public:
	~MCU();
	
private:
	MCU();
	
	MCU(const MCU &) = delete;
	MCU &operator=(const MCU &) = delete;

private:
	std::string firmware_version;
	std::string product_name;
	int mcu_version;
	bool power_enough;
	std::shared_ptr<IIC> iic;
	std::string gps_cached_data;
};

#endif
