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

	//系统类参数
	int readFworkMark();
	int readEventStatus();
	int readPType();

	//基础信息类参数
	int readUWS();
	int readCDS_DN();
	int readCDS_Value();
	int readVTSAlarm();
	int readVTSSens();
	int readNUFQ();
	int readTimeout();
	int readCamStatus();
	int readAIAlarm();

	//信号类参数
	std::string readSignalType();
	int readSignalRL();

	//传感器类参数
	int readSOR_AL();
	int readSOR_UVL();
	int readSOR_NOISE();
	int readSOR_CO();
	int readSOR_CO2();
	int readSOR_O2();

	//外扩无线设备类参数
	int readESOR_WS();
	int readESOR_WID();
	int readESOR_ADD();
	int readESOR_ID();
	int readESOR_TYPE();
	int readESOR_BAT();
	int readESOR_GPSA();
	int readESOR_GPSL();
	int readESOR_GPSH();
	unsigned int readESOR_Value();
	bool writeESOR_WS(int ws);
	bool writeESOR_WID(int wid);

	//参数设置类参数
	int readCAM_MAXS();
	int readPIR_MODE();
	int readPIR_SENS();
	int readPIR_INT();
	int readTIMER();
	int readPIR_EN();
	int readTIMER_INT();
	int readTIMER_1START();
	int readTIMER_1END();
	int readTIMER_2START();
	int readTIMER_2END();
	int readTIMER_3START();
	int readTIMER_3END();
	int readTIMER_4START();
	int readTIMER_4END();
	int readTIMER_5START();
	int readTIMER_5END();
	int readTIMER_REPEATS();
	std::string readDEVICE_NAME();
	int readHEARTRATE();
	int readUP_MODE();
	int readUP_NUFQ();
	int readTDS_CF();
	int readTDS_TP();
	int readTDS_BW();

	bool writeCAM_MAXS(int max);
	bool writePIR_MODE(int mode);
	bool writePIR_SENS(int sens);
	bool writePIR_INT(int interval);
	bool writeTIMER(int timer);
	bool writePIR_EN(int en);
	bool writeTIMER_INT(int interval);
	bool writeTIMER_1START(int time);
	bool writeTIMER_1END(int time);
	bool writeTIMER_2START(int time);
	bool writeTIMER_2END(int time);
	bool writeTIMER_3START(int time);
	bool writeTIMER_3END(int time);
	bool writeTIMER_4START(int time);
	bool writeTIMER_4END(int time);
	bool writeTIMER_5START(int time);
	bool writeTIMER_5END(int time);
	bool writeTIMER_REPEATS(int repeats);
	bool writeDEVICE_NAME(const std::string &name);
	bool writeHEARTRATE(int rate);
	bool writeUP_MODE(int mode);
	bool writeUP_NUFQ(int num);
	bool writeTDS_CF(int cf);
	bool writeTDS_TP(int tp);
	bool writeTDS_BW(int bw);

	// 单元测试函数
	void readAllTestData();

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
