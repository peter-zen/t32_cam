#include "MCU.h"
#include <mutex>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vector>
#include <sstream>
#include "Logger.h"
#include "RTC.h"
#include "StringConvert.h"
#include "Common.h"

#define I2C_SLAVE_NAME "/dev/hc32l13x"


std::shared_ptr<MCU> MCU::getInstance()
{
	static std::shared_ptr<MCU> instance;
	static std::once_flag initInstanceFlag;

	std::call_once(initInstanceFlag, []() { instance.reset(new MCU()); });
	return instance;
}

MCU::MCU()
	:mcu_version(0)
{
	iic = std::make_shared<IIC>(I2C_SLAVE_NAME);
	if (iic) {
		iic->open();
	}
}

MCU::~MCU()
{
	if (iic) {
		iic->close();
	}
}

std::string MCU::readFirmwareVersion()
{
	if (firmware_version.empty()) {
		firmware_version = "1.0.0";
	}
	return firmware_version;
}

bool MCU::powerEnoughForFirmwareUpdate()
{
	if (power_enough) {
		return true;
	}
	return false;
}

bool MCU::waitFor(int seconds)
{
	return false;
}

int MCU::readShutdownVoltage()
{
	return 0;
}

int MCU::readLowPowerVoltage()
{
	return 0;
}

int MCU::readBatteryLevel()
{
	return 0;
}

int MCU::readBatteryType()
{
	return 0;
}

bool MCU::IsWifiStationReady()
{
	return true;
}

int MCU::readSignalCF()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_CF);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_CF);
	
	// 带重试的I2C读取
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000); // 等待10ms后重试
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SIG_CF failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

int MCU::readSignalRSSI()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_RSSI);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_RSSI);
	
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SIG_RSSI failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

int MCU::readSignalRSRP()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_RSRP);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_RSRP);
	
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SIG_RSRP failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

int MCU::readSignalRSRQ()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_RSRQ);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_RSRQ);
	
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SIG_RSRQ failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

int MCU::readSignalSNR()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_SNR);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_SNR);
	
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SIG_SNR failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

int MCU::readSignalTD()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_TD);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_TD);
	
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SIG_TD failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

int MCU::readSignalTP()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_TP);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_TP);
	
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SIG_TP failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

bool MCU::Is4gExist()
{
	return false;
}

bool MCU::writeRemoteWakeup(int remote_wakeup)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &remote_wakeup, sizeof(remote_wakeup));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UWS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UWS);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write remote wakeup failed");
		return false;
	}
	return true;
}

bool MCU::useGpsTime()
{
	return false;
}

int MCU::readCds()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_CDS_VALUE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_CDS_VALUE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	return value;
}

bool MCU::IsRemoteWakeup()
{
	int value = readUWS();
	return (value != 0);
}

int MCU::readRMID()
{
	return 0;
}

int MCU::readRMType()
{
	return 0;
}

int MCU::readRMValue()
{
	return 0;
}

int MCU::readRMCount()
{
	return 0;
}
int MCU::readEventType()
{
	return 0;
}

int MCU::readEventID()
{
	return 0;
}

int MCU::readEventNum()
{
	return 0;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int MCU::readWorkingMode()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_MODE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_MODE);

	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read working mode failed");
		return -1;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::ERROR, "[MCU]working mode: %d", value);
	return value;
}

int MCU::readTemperature()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_TEMPS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_TEMPS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read temperature failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]temperature: %d", value - 125);
	return value - 125;
}

int MCU::readHumidity()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_RHS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_RHS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read humidity failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]humidity: %d", value);
	return value;
}

int MCU::readAtmosPressure()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_APS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_APS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read atmos pressure failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]atmos pressure: %d", value/10);
	return value/10;
}

int MCU::readVersion()
{
	if (mcu_version != 0) {
		return mcu_version;
	}

	char *pval = (char *)&mcu_version;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_VERSION);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_VERSION);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read version failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}

	return mcu_version;
}

std::string MCU::convertVersion(int ver)
{
	// 转换版本号格式：数字值 -> Vxx.xxx
	// 例如：10002 -> V10.002
	int major = ver / 1000;
	int minor = ver % 1000;
	char version_str[32];
	snprintf(version_str, sizeof(version_str), "V%02d.%03d", major, minor);
	Logger::log(LogLevel::INFO, "[MCU]version: %s", version_str);
	return version_str;
}

std::string MCU::readPID()
{
	char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PID);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PID);
	
	// 带重试的I2C读取
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read PID failed");
		return "";
	}
	
	// 检查ASCII码值范围 (32~127)
	for (int i = 0; i < nbytes && buf[i] != '\0'; i++) {
		if (buf[i] < 32 || buf[i] > 127) {
			Logger::log(LogLevel::ERROR, "[MCU]PID contains invalid ASCII character at position %d: 0x%02X", i, (unsigned char)buf[i]);
			return "";
		}
	}
	
	std::string pid = buf;
	Logger::log(LogLevel::INFO, "[MCU]PID: %s", pid.c_str());
	return pid;
}

std::string MCU::readUPID()
{
	char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UPID);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UPID);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read UPID failed");
		return "";
	}
	
	// 检查ASCII码值范围 (32~127)
	for (int i = 0; i < nbytes && buf[i] != '\0'; i++) {
		if (buf[i] < 32 || buf[i] > 127) {
			Logger::log(LogLevel::ERROR, "[MCU]UPID contains invalid ASCII character at position %d: 0x%02X", i, (unsigned char)buf[i]);
			return "";
		}
	}
	
	std::string upid = buf;
	Logger::log(LogLevel::INFO, "[MCU]UPID: %s", upid.c_str());
	return upid;
}

std::string MCU::readUPWD()
{
	char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UPWD);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UPWD);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read UPWD failed");
		return "";
	}
	
	// 检查ASCII码值范围 (32~127)
	for (int i = 0; i < nbytes && buf[i] != '\0'; i++) {
		if (buf[i] < 32 || buf[i] > 127) {
			Logger::log(LogLevel::ERROR, "[MCU]UPWD contains invalid ASCII character at position %d: 0x%02X", i, (unsigned char)buf[i]);
			return "";
		}
	}
	
	std::string upwd = buf;
	Logger::log(LogLevel::INFO, "[MCU]UPWD: %s", upwd.c_str());
	return upwd;
}

bool MCU::writePID(const std::string &pid)
{
	if (pid.empty()) {
		return false;
	}

	unsigned char *buf = (unsigned char *)pid.c_str();
	int nbytes = pid.length();
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PID);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write PID failed");
	    return false;
	}

	return true;
}
bool MCU::writeUPID(const std::string &upid)
{
	if (upid.empty()) {
		return false;
	}
	
	unsigned char *buf = (unsigned char *)upid.c_str();
	int nbytes = upid.length();
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UPID);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write UPID failed");
	    return false;
	}

	return true;
}
bool MCU::writeUPWD(const std::string &password)
{
	if (password.empty()) {
		return false;
	}
	
	unsigned char *buf = (unsigned char *)password.c_str();
	int nbytes = password.length();
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UPWD);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write UPWD failed");
	    return false;
	}

	return true;
}

int MCU::readBatteryVoltage()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_BAT1);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_BAT1);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read battery voltage failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]battery voltage: %d", value);
	return value;
}

int MCU::readBattery1Voltage()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_BAT1);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_BAT1);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read battery1 voltage failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]battery1 voltage: %d", value);
	return value;
}

int MCU::readBattery2Voltage()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_BAT2);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_BAT2);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read battery2 voltage failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]battery2 voltage: %d", value);
	return value;
}

int MCU::readRMSunPowerValue()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SPWR);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SPWR);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read RMSunPowerValue failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]RMSunPowerValue: %d", value);
	return value;
}

int MCU::readExternalVoltage()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_EPWR);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_EPWR);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read external voltage failed");
		return 0;
	}

	for (int i = 0; i < nbytes; i++) {
	    pval[i] = buf[i];
	}
	Logger::log(LogLevel::INFO, "[MCU]external voltage: %d", value);
	return value;
}

bool MCU::setDatetime(const struct tm *time)
{
	if (time == NULL) {
		return false;
	}

	Logger::log(LogLevel::INFO, "%04d-%02d-%02d %02d:%02d:%02d", time->tm_year + YEAR_OFFSET, time->tm_mon + MONTH_OFFSET, time->tm_mday,
		    time->tm_hour, time->tm_min, time->tm_sec);

	unsigned char buf[32];
	int nbytes = 0;
	int reg_start = 0;
	{//year
		int year = (time->tm_year + YEAR_OFFSET);
		reg_start = PARAM_UNPACK_START(PARAM_MCU_YEAR);
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_YEAR);
		memset(&buf[0], 0, sizeof(buf));
		memcpy(&buf[0], &year, nbytes);
		if (iic->write(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to write year to MCU");
			return false;
		}
	}

	{//month
		int month = (time->tm_mon + MONTH_OFFSET);
		reg_start = PARAM_UNPACK_START(PARAM_MCU_MONTH);
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_MONTH);
		memset(&buf[0], 0, sizeof(buf));
		memcpy(&buf[0], &month, nbytes);
		if (iic->write(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to write month to MCU");
			return false;
		}
	}

	{//day
		int day = time->tm_mday;
		reg_start = PARAM_UNPACK_START(PARAM_MCU_DAY);
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_DAY);
		memset(&buf[0], 0, sizeof(buf));
		memcpy(&buf[0], &day, nbytes);
		if (iic->write(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to write day to MCU");
			return false;
		}
	}
	
	{//hour
		int hour = time->tm_hour;
		reg_start = PARAM_UNPACK_START(PARAM_MCU_HOUR);
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_HOUR);
		memset(&buf[0], 0, sizeof(buf));
		memcpy(&buf[0], &hour, nbytes);
		if (iic->write(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to write hour to MCU");
			return false;
		}
	}

	{//minute
		int minute = time->tm_min;
		reg_start = PARAM_UNPACK_START(PARAM_MCU_MINUTE);
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_MINUTE);
		memset(&buf[0], 0, sizeof(buf));
		memcpy(&buf[0], &minute, nbytes);
		if (iic->write(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to write minute to MCU");
			return false;
		}
	}

	{//second
		int second = time->tm_sec;
		reg_start = PARAM_UNPACK_START(PARAM_MCU_SECOND);
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SECOND);
		memset(&buf[0], 0, sizeof(buf));
		memcpy(&buf[0], &second, nbytes);
		if (iic->write(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to write second to MCU");
			return false;
		}
	}

	return true;
}

struct tm MCU::getDatetime()
{
	struct tm time;
	memset(&time, 0, sizeof(time));
	unsigned char buf[32];
	
	// 一次性读取所有日期时间字段（从YEAR开始，共7字节：YEAR(2) + MONTH(1) + DAY(1) + HOUR(1) + MINUTE(1) + SECOND(1)）
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_YEAR);
	int total_bytes = PARAM_UNPACK_BYTES(PARAM_MCU_YEAR) + 
					   PARAM_UNPACK_BYTES(PARAM_MCU_MONTH) + 
					   PARAM_UNPACK_BYTES(PARAM_MCU_DAY) + 
					   PARAM_UNPACK_BYTES(PARAM_MCU_HOUR) + 
					   PARAM_UNPACK_BYTES(PARAM_MCU_MINUTE) + 
					   PARAM_UNPACK_BYTES(PARAM_MCU_SECOND);
	
	memset(buf, 0, sizeof(buf));
	if (iic->read(reg_start, buf, total_bytes) <= 0) {
		Logger::log(LogLevel::ERROR, "Failed to read datetime from MCU");
		return time;
	}
	
	// 解析读取的数据
	int offset = 0;
	
	// year (2 bytes)
	int year = 0;
	memcpy(&year, buf + offset, PARAM_UNPACK_BYTES(PARAM_MCU_YEAR));
	offset += PARAM_UNPACK_BYTES(PARAM_MCU_YEAR);
	time.tm_year = year - YEAR_OFFSET;
	
	// month (1 byte)
	time.tm_mon = buf[offset++] - MONTH_OFFSET;
	
	// day (1 byte)
	time.tm_mday = buf[offset++];
	
	// hour (1 byte)
	time.tm_hour = buf[offset++];
	
	// minute (1 byte)
	time.tm_min = buf[offset++];
	
	// second (1 byte)
	time.tm_sec = buf[offset++];

	// 记录读取的时间信息到日志
	Logger::log(LogLevel::INFO, "Read datetime from MCU: %04d-%02d-%02d %02d:%02d:%02d", 
		time.tm_year + YEAR_OFFSET, time.tm_mon + MONTH_OFFSET, time.tm_mday,
		time.tm_hour, time.tm_min, time.tm_sec);

	return time;
}


std::string MCU::readGps()
{
    // if cached gps data, return it
    if (!gps_cached_data.empty()) {
        return gps_cached_data;
    }
	unsigned char buf[32] = { 0 };

    int longitude;
    int latitude;
    int altitude;
    char longitude_direction = 0;
    char latitude_direction = 0;
	int nbytes = 0;
	int reg_start = 0;

    //longitude - 使用MCU本身的GPS经度地址
    {
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_LOCTION_LON);
		reg_start = PARAM_UNPACK_START(PARAM_MCU_LOCTION_LON);
		memset(buf, 0, sizeof(buf));
		if (iic->read(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read longitude from MCU");
			gps_cached_data = ",,,,,";
			return gps_cached_data;
		}
		
		memcpy(&longitude, buf, nbytes);
		if (longitude > 0) {
			longitude_direction = 'E'; // east
		} else if (longitude < 0) {
			longitude_direction = 'W'; // west 
		} else {
			gps_cached_data = ",,,,,";
        	return gps_cached_data;
		}
    }

    // read latitude data - 使用MCU本身的GPS纬度地址
	{
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_LOCTION_LAT);
		reg_start = PARAM_UNPACK_START(PARAM_MCU_LOCTION_LAT);
		memset(buf, 0, sizeof(buf));
		if (iic->read(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read latitude from MCU");
			gps_cached_data = ",,,,,";
			return gps_cached_data;
		}
		memcpy(&latitude, buf, nbytes);
		if (latitude > 0) {
			latitude_direction = 'N'; // north
		} else if (latitude < 0) {
			latitude_direction = 'S'; // south
		} else {
			gps_cached_data = ",,,,,";
        	return gps_cached_data;
		}
    }

    // altitude - 使用MCU本身的GPS高程地址
	{
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_LOCTION_ELE);
		reg_start = PARAM_UNPACK_START(PARAM_MCU_LOCTION_ELE);
		memset(buf, 0, sizeof(buf));
		if (iic->read(reg_start, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read altitude from MCU");
			gps_cached_data = ",,,,,";
			return gps_cached_data;
		}
		memcpy(&altitude, buf, nbytes);
		if (altitude == 0) {
			gps_cached_data = ",,,,,";
			return gps_cached_data;
		}
	}

    // assemble gps data string
    gps_cached_data = to_string_custom(abs(longitude) / 10000000.0) + "," + longitude_direction + "," + to_string_custom(abs(latitude) / 10000000.0) + "," + latitude_direction + "," +
                      to_string_custom(altitude / 10.0);
   

    return gps_cached_data;
}

bool MCU::writeGps(const std::string &gps)
{
	// 解析GPS字符串，格式应该为：longitude,longitude_direction,latitude,latitude_direction,altitude
	std::vector<std::string> parts;
	std::stringstream ss(gps);
	std::string part;
	
	while (std::getline(ss, part, ',')) {
		parts.push_back(part);
	}
	
	// 检查格式是否正确
	if (parts.size() != 5) {
		Logger::log(LogLevel::ERROR, "Invalid GPS format, expected: longitude,longitude_direction,latitude,latitude_direction,altitude");
		return false;
	}
	
	try {
		// 解析经纬度和高度为整数
		int longitude = stoi_custom(parts[0]) * 10000000;//x10^7
		char longitude_direction = !parts[1].empty() ? parts[1][0] : 0;
		int latitude = stoi_custom(parts[2]) * 10000000;//x10^7
		char latitude_direction = !parts[3].empty() ? parts[3][0] : 0;
		int altitude = stoi_custom(parts[4]) * 10;//x10
		
		// 根据方向字符设置经纬度的正负值
		if (longitude_direction == 'W' || longitude_direction == 'w') {
			longitude = -longitude;
		} else if (longitude_direction != 'E' && longitude_direction != 'e') {
			Logger::log(LogLevel::ERROR, "Invalid longitude direction, expected 'E' or 'W'");
			return false;
		}
		
		if (latitude_direction == 'S' || latitude_direction == 's') {
			latitude = -latitude;
		} else if (latitude_direction != 'N' && latitude_direction != 'n') {
			Logger::log(LogLevel::ERROR, "Invalid latitude direction, expected 'N' or 'S'");
			return false;
		}
		
		// 验证数据有效性
		if (longitude == 0 || latitude == 0 || altitude == 0) {
			Logger::log(LogLevel::ERROR, "Invalid GPS data, values cannot be zero");
			return false;
		}
		
		unsigned char buf[32] = { 0 };
		int nbytes = 0;
		bool success = true;
		
		// 写入经度数据
		{
			nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_LOCTION_LON);
			memset(buf, 0, sizeof(buf));
			memcpy(buf, &longitude, nbytes);
			if (iic->write(PARAM_UNPACK_START(PARAM_MCU_LOCTION_LON), buf, nbytes) <= 0) {
				Logger::log(LogLevel::ERROR, "Failed to write longitude to MCU");
				success = false;
			}
		}
		
		// 写入纬度数据
		if (success) {
			nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_LOCTION_LAT);
			memset(buf, 0, sizeof(buf));
			memcpy(buf, &latitude, nbytes);
			if (iic->write(PARAM_UNPACK_START(PARAM_MCU_LOCTION_LAT), buf, nbytes) <= 0) {
				Logger::log(LogLevel::ERROR, "Failed to write latitude to MCU");
				success = false;
			}
		}
		
		// 写入高度数据
		if (success) {
			nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_LOCTION_ELE);
			memset(buf, 0, sizeof(buf));
			memcpy(buf, &altitude, nbytes);
			if (iic->write(PARAM_UNPACK_START(PARAM_MCU_LOCTION_ELE), buf, nbytes) <= 0) {
				Logger::log(LogLevel::ERROR, "Failed to write altitude to MCU");
				success = false;
			}
		}
		
		// 如果写入成功，清除缓存，确保下次读取时获取新数据
		if (success) {
			gps_cached_data.clear();
			Logger::log(LogLevel::INFO, "GPS data written to MCU successfully: longitude=%d, latitude=%d, altitude=%d", 
				longitude, latitude, altitude);
		}
		
		return success;
	}
	catch (const std::invalid_argument& e) {
		Logger::log(LogLevel::ERROR, "Invalid GPS data format: %s", e.what());
		return false;
	}
	catch (const std::out_of_range& e) {
		Logger::log(LogLevel::ERROR, "GPS data value out of range: %s", e.what());
		return false;
	}
}

std::string MCU::convertVoltage(int value)
{
	char buf[32] = {0};
	snprintf(buf, sizeof(buf), "%d.%d", value / 10, (value % 10) / 10);
	return std::string(buf);
}

//系统类参数
int MCU::readFworkMark()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_FWORK_MARK);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_FWORK_MARK);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read fwork mark failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readEventStatus()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_EVENT_STATUS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_EVENT_STATUS);
	
	// 带重试的I2C读取
	int retries = 3;
	int read_result = -1;
	while (retries-- > 0 && read_result <= 0) {
	    read_result = iic->read(reg_start, &buf[0], nbytes);
	    if (read_result <= 0 && retries > 0) {
	        usleep(10000);
	    }
	}
	
	if (read_result <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read event status failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readPType()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PType);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PType);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read PType failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

//基础信息类参数
int MCU::readUWS()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UWS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UWS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read UWS failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readCDS_DN()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_CDS_DN);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_CDS_DN);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read CDS_DN failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readCDS_Value()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_CDS_VALUE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_CDS_VALUE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read CDS_VALUE failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readVTSAlarm()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_VTS_ALARM);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_VTS_ALARM);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read VTS_ALARM failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readVTSSens()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_VTS_SENS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_VTS_SENS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read VTS_SENS failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readNUFQ()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_NUFQ);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_NUFQ);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read NUFQ failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTimeout()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMEOUT);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMEOUT);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMEOUT failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readCamStatus()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_CAM_STATUS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_CAM_STATUS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read CAM_STATUS failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readAIAlarm()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_AI_ALARM);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_AI_ALARM);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read AI_ALARM failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

//信号类参数
std::string MCU::readSignalType()
{
	char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_TYPE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_TYPE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read signal type failed");
		return "";
	}
	return std::string(buf);
}

int MCU::readSignalRL()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SIG_RL);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SIG_RL);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read signal RL failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

//传感器类参数
int MCU::readSOR_AL()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_AL);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_AL);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SOR_AL failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readSOR_UVL()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_UVL);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_UVL);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SOR_UVL failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readSOR_NOISE()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_NOISE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_NOISE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SOR_NOISE failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readSOR_CO()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_CO);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_CO);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SOR_CO failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readSOR_CO2()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_CO2);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_CO2);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SOR_CO2 failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readSOR_O2()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SOR_O2);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SOR_O2);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read SOR_O2 failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

//外扩无线设备类参数
int MCU::readESOR_ADD()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_ADD);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_ADD);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_ADD failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readESOR_ID()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_ID);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_ID);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_ID failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readESOR_TYPE()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_TYPE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_TYPE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_TYPE failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readESOR_BAT()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_BAT);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_BAT);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_BAT failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readESOR_GPSA()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_GPSA);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_GPSA);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_GPSA failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readESOR_GPSL()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_GPSL);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_GPSL);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_GPSL failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readESOR_GPSH()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_GPSH);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_GPSH);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_GPSH failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

unsigned int MCU::readESOR_Value()
{
	unsigned int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_VALUE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_VALUE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_VALUE failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

bool MCU::writeESOR_WS(int ws)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &ws, sizeof(ws));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_WS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_WS);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write ESOR_WS failed");
		return false;
	}
	return true;
}

bool MCU::writeESOR_WID(int wid)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &wid, sizeof(wid));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_WID);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_WID);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write ESOR_WID failed");
		return false;
	}
	return true;
}

int MCU::readESOR_WS()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_WS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_WS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_WS failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readESOR_WID()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_ESOR_WID);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_ESOR_WID);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read ESOR_WID failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

//参数设置类参数
int MCU::readCAM_MAXS()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_CAM_MAXS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_CAM_MAXS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read CAM_MAXS failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readPIR_MODE()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_MODE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_MODE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read PIR_MODE failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readPIR_SENS()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_SENS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_SENS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read PIR_SENS failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readPIR_INT()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_INT);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_INT);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read PIR_INT failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readPIR_EN()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_EN);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_EN);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read PIR_EN failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_INT()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_INT);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_INT);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_INT failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_1START()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_1START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_1START);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_1START failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_1END()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_1END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_1END);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_1END failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_2START()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_2START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_2START);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_2START failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_2END()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_2END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_2END);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_2END failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_3START()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_3START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_3START);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_3START failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_3END()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_3END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_3END);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_3END failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_4START()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_4START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_4START);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_4START failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_4END()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_4END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_4END);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_4END failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_5START()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_5START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_5START);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_5START failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_5END()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_5END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_5END);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_5END failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTIMER_REPEATS()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_REPEATS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_REPEATS);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TIMER_REPEATS failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

std::string MCU::readDEVICE_NAME()
{
	char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_DEVICE_NAME);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_DEVICE_NAME);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read DEVICE_NAME failed");
		return "";
	}
	return std::string(buf);
}

int MCU::readHEARTRATE()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_HEARTRATE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_HEARTRATE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read HEARTRATE failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readUP_MODE()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UP_MODE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UP_MODE);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read UP_MODE failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readUP_NUFQ()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UP_NUFQ);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UP_NUFQ);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read UP_NUFQ failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTDS_CF()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TDS_CF);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TDS_CF);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TDS_CF failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTDS_TP()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TDS_TP);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TDS_TP);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TDS_TP failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

int MCU::readTDS_BW()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TDS_BW);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TDS_BW);
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read TDS_BW failed");
		return 0;
	}
	for (int i = 0; i < nbytes; i++) {
		pval[i] = buf[i];
	}
	return value;
}

bool MCU::writeCAM_MAXS(int max)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &max, sizeof(max));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_CAM_MAXS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_CAM_MAXS);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write CAM_MAXS failed");
		return false;
	}
	return true;
}

bool MCU::writePIR_MODE(int mode)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &mode, sizeof(mode));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_MODE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_MODE);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write PIR_MODE failed");
		return false;
	}
	return true;
}

bool MCU::writePIR_SENS(int sens)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &sens, sizeof(sens));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_SENS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_SENS);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write PIR_SENS failed");
		return false;
	}
	return true;
}

bool MCU::writePIR_INT(int interval)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &interval, sizeof(interval));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_INT);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_INT);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write PIR_INT failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER(int timer)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &timer, sizeof(timer));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER failed");
		return false;
	}
	return true;
}

bool MCU::writePIR_EN(int en)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &en, sizeof(en));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_PIR_EN);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_PIR_EN);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write PIR_EN failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_INT(int interval)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &interval, sizeof(interval));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_INT);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_INT);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_INT failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_1START(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_1START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_1START);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_1START failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_1END(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_1END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_1END);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_1END failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_2START(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_2START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_2START);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_2START failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_2END(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_2END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_2END);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_2END failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_3START(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_3START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_3START);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_3START failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_3END(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_3END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_3END);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_3END failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_4START(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_4START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_4START);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_4START failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_4END(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_4END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_4END);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_4END failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_5START(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_5START);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_5START);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_5START failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_5END(int time)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &time, sizeof(time));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_5END);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_5END);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_5END failed");
		return false;
	}
	return true;
}

bool MCU::writeTIMER_REPEATS(int repeats)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &repeats, sizeof(repeats));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TIMER_REPEATS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TIMER_REPEATS);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TIMER_REPEATS failed");
		return false;
	}
	return true;
}

bool MCU::writeDEVICE_NAME(const std::string &name)
{
	if (name.empty()) {
		return false;
	}
	unsigned char *buf = (unsigned char *)name.c_str();
	int nbytes = name.length();
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_DEVICE_NAME);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write DEVICE_NAME failed");
		return false;
	}
	return true;
}

bool MCU::writeHEARTRATE(int rate)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &rate, sizeof(rate));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_HEARTRATE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_HEARTRATE);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write HEARTRATE failed");
		return false;
	}
	return true;
}

bool MCU::writeUP_MODE(int mode)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &mode, sizeof(mode));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UP_MODE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UP_MODE);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write UP_MODE failed");
		return false;
	}
	return true;
}

bool MCU::writeUP_NUFQ(int num)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &num, sizeof(num));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_UP_NUFQ);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_UP_NUFQ);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write UP_NUFQ failed");
		return false;
	}
	return true;
}

bool MCU::writeTDS_CF(int cf)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &cf, sizeof(cf));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TDS_CF);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TDS_CF);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TDS_CF failed");
		return false;
	}
	return true;
}

bool MCU::writeTDS_TP(int tp)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &tp, sizeof(tp));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TDS_TP);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TDS_TP);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TDS_TP failed");
		return false;
	}
	return true;
}

bool MCU::writeTDS_BW(int bw)
{
	unsigned char buf[128] = { 0 };
	memcpy(buf, &bw, sizeof(bw));
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TDS_BW);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TDS_BW);
	if (iic->write(reg_start, buf, nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]write TDS_BW failed");
		return false;
	}
	return true;
}

/**
 * @brief MCU I2C参数单元测试 - 读取并打印所有测试数据
 * 
 * 此函数用于从MCU读取测试数据，验证I2C通信是否正常
 */
void MCU::readAllTestData()
{
	Logger::log(LogLevel::INFO, "\n========== MCU I2C Test Data Read ==========");

	// ========== 系统核心参数 (0x0000-0x000F) ==========
	Logger::log(LogLevel::INFO, "[0x0000] MODE: %d (0-初始,1-单拍,2-拍传,3-传输,4-测试)", 
				readWorkingMode());
	Logger::log(LogLevel::INFO, "[0x0001] FWORK_MARK: %d", readFworkMark());
	Logger::log(LogLevel::INFO, "[0x0002] EVENT_STATUS: %d", readEventStatus());
	Logger::log(LogLevel::INFO, "[0x0003] PType: %d", readPType());
	
	int version = readVersion();
	Logger::log(LogLevel::INFO, "[0x0004] VERSION: %d (%s)", version, convertVersion(version).c_str());

	// ========== 基础信息参数 (0x0010-0x00FF) ==========
	int bat1 = readBattery1Voltage();
	int bat2 = readBattery2Voltage();
	int epwr = readExternalVoltage();
	int spwr = readRMSunPowerValue();
	
	Logger::log(LogLevel::INFO, "[0x0010] BAT1: %d.%dV", bat1 / 10, bat1 % 10);
	Logger::log(LogLevel::INFO, "[0x0011] BAT2: %d.%dV", bat2 / 10, bat2 % 10);
	Logger::log(LogLevel::INFO, "[0x0012] EPWR: %d.%dV", epwr / 10, epwr % 10);
	Logger::log(LogLevel::INFO, "[0x0013] SPWR: %d.%dV", spwr / 10, spwr % 10);

	// GPS参数
	std::string gps = readGps();
	Logger::log(LogLevel::INFO, "[0x0014] GPS: %s", gps.c_str());

	std::string pid = readPID();
	Logger::log(LogLevel::INFO, "[0x001E] PID: %s", pid.c_str());

	Logger::log(LogLevel::INFO, "[0x003E] UWS: %d (0-无,1-433,2-LoRa)", readUWS());

	// RTC时间
	struct tm dt = getDatetime();
	Logger::log(LogLevel::INFO, "[0x009F] DATETIME: %04d-%02d-%02d %02d:%02d:%02d", 
				dt.tm_year + 1900, dt.tm_mon + 1, dt.tm_mday,
				dt.tm_hour, dt.tm_min, dt.tm_sec);

	Logger::log(LogLevel::INFO, "[0x00A6] NUFQ: %d", readNUFQ());
	Logger::log(LogLevel::INFO, "[0x00A8] TIMEOUT: %d秒", readTimeout());
	Logger::log(LogLevel::INFO, "[0x00AA] CAM_STATUS: %d (0-已关,1-已开机,2-连网成功,3-连服务器成功,4-上传中)", readCamStatus());
	Logger::log(LogLevel::INFO, "[0x00AB] AI_ALARM: %d", readAIAlarm());

	// ========== 信号遥测参数 (0x0100-0x01FF) ==========
	Logger::log(LogLevel::INFO, "[0x0100] SIG_TYPE: %s", readSignalType().c_str());
	Logger::log(LogLevel::INFO, "[0x010C] SIG_CF: %d MHz", readSignalCF());
	Logger::log(LogLevel::INFO, "[0x010E] SIG_TP: %d dBm", readSignalTP());
	Logger::log(LogLevel::INFO, "[0x010F] SIG_RSSI: %d dBm", readSignalRSSI());
	Logger::log(LogLevel::INFO, "[0x0110] SIG_RSRP: %d dBm", readSignalRSRP());
	Logger::log(LogLevel::INFO, "[0x0111] SIG_RSRQ: %d dBm", readSignalRSRQ());
	Logger::log(LogLevel::INFO, "[0x0112] SIG_RL: %d dB", readSignalRL());
	Logger::log(LogLevel::INFO, "[0x0114] SIG_SNR: %d", readSignalSNR());
	Logger::log(LogLevel::INFO, "[0x0115] SIG_TD: %d m", readSignalTD());

	// ========== 传感器遥测参数 (0x0200-0x02FF) ==========
	Logger::log(LogLevel::INFO, "[0x0200] CDS_DN: %d (0-夜晚,1-白天)", readCDS_DN());
	Logger::log(LogLevel::INFO, "[0x0201] CDS_VALUE: %d.%dV", readCDS_Value() / 10, readCDS_Value() % 10);
	Logger::log(LogLevel::INFO, "[0x0202] VTS_ALARM: %d (0-无振动,1-振动报警)", readVTSAlarm());
	Logger::log(LogLevel::INFO, "[0x0203] VTS_SENS: %d (0-无,1-低,2-中,3-高)", readVTSSens());
	
	int temp = readTemperature();
	int humidity = readHumidity();
	int pressure = readAtmosPressure();
	
	Logger::log(LogLevel::INFO, "[0x0204] TEMP: %dC", temp);
	Logger::log(LogLevel::INFO, "[0x0205] HUMIDITY: %d%%", humidity);
	Logger::log(LogLevel::INFO, "[0x0206] PRESSURE: %d.%d hPa", pressure / 10, pressure % 10);

	Logger::log(LogLevel::INFO, "[0x0208] CO: %d ppm", readSOR_CO());
	Logger::log(LogLevel::INFO, "[0x020A] CO2: %d ppm", readSOR_CO2());
	Logger::log(LogLevel::INFO, "[0x020C] O2: %d%%", readSOR_O2());
	Logger::log(LogLevel::INFO, "[0x020D] AL: %d lx", readSOR_AL());
	Logger::log(LogLevel::INFO, "[0x020F] UVL: %d", readSOR_UVL());
	Logger::log(LogLevel::INFO, "[0x0210] NOISE: %d dBA", readSOR_NOISE());

	// ========== 外扩无线设备参数 (0x0300-0x03FF) ==========
	Logger::log(LogLevel::INFO, "[0x0300] ESOR_WS: %d (0-无,1-433,2-LoRa)", readESOR_WS());
	Logger::log(LogLevel::INFO, "[0x0301] ESOR_WID: %d", readESOR_WID());
	Logger::log(LogLevel::INFO, "[0x0303] ESOR_ADD: %d", readESOR_ADD());
	Logger::log(LogLevel::INFO, "[0x0305] ESOR_ID: %d", readESOR_ID());
	Logger::log(LogLevel::INFO, "[0x0307] ESOR_TYPE: %d", readESOR_TYPE());
	Logger::log(LogLevel::INFO, "[0x0308] ESOR_BAT: %d.%dV", readESOR_BAT() / 10, readESOR_BAT() % 10);

	// ========== 设置/策略参数 (0x0400-0x04FF) ==========
	Logger::log(LogLevel::INFO, "[0x0400] CAM_MAXS: %d", readCAM_MAXS());
	Logger::log(LogLevel::INFO, "[0x0401] PIR_MODE: %d (0-关闭,1-主副同触发,2-主探测)", readPIR_MODE());
	Logger::log(LogLevel::INFO, "[0x0402] PIR_SENS: %d (0-自动,1-低,2-中,3-高)", readPIR_SENS());
	Logger::log(LogLevel::INFO, "[0x0403] PIR_INT: %d秒", readPIR_INT());
	Logger::log(LogLevel::INFO, "[0x0405] TIMER: %d (0-关闭,1-开启)", readTIMER());
	Logger::log(LogLevel::INFO, "[0x0406] PIR_EN: %d (0-关闭,1-开启)", readPIR_EN());
	Logger::log(LogLevel::INFO, "[0x0407] TIMER_INT: %d分钟", readTIMER_INT());

	Logger::log(LogLevel::INFO, "=============================================");
}