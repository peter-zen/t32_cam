#ifndef MCU_H
#define MCU_H
#include <string>
#include <memory>
#include "I2C.h"

class MCU {
    public:
	static std::shared_ptr<MCU> getInstance();
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
	int readRMSunPowerValue();
	int readRMCount();
	int readEventType();
	int readEventID();
	int readEventNum();

	std::string readVersion();
	~MCU();
	
    private:
	MCU();
	
	MCU(const MCU &) = delete;
	MCU &operator=(const MCU &) = delete;

    private:
	std::string firmware_version;
	std::string product_name;
	bool power_enough;
	std::shared_ptr<I2C> i2c;
};

#endif
