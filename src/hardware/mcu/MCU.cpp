#include "MCU.h"
#include <mutex>
#include <string.h>
#include <time.h>
#include <vector>
#include <sstream>
#include "Logger.h"
#include "time/rtc/RTC.h"
#include "StringConvert.h"
#include "Common.h"

#define I2C_SLAVE_NAME "/dev/hc32l13x"

enum {
	PARAM_STATUS=1, /*工作状，参考STATUS_BIT_E*/
	PARAM_IR_VALUE_H, /*光敏值 高8bit*/
	PARAM_IR_VALUE, /*光敏值 低8bit*/
	PARAM_BATTERY, /*电池电量等级0-3，3最高，0 为电池电量低，1-3代表电池电量低中高3个等级**/
	PARAM_BAT_H, /*电池电量电压值 高8bit*/
	PARAM_BAT_L, /*电池电量电压值 低8bit*/
	PARAM_TEMPER, /*热敏电阻值0-216，减40后转换为实际的华氏度值，摄氏度测量范围（-40C ~ 80C）**/
	PARAM_BAT_TYPE, /*电池类型BATTERY_TYPE_E，根据电池电压来区分，上电时大于8.5V为干电池；0 18650理电池，1 1.5V干电池，暂不能匹配1.2V电池*/
	//PARAM_MCU_VERSION, /*DSP需转换成字符串上传给遥控器，例：100 转换为"100"*/

	PARAM_NOT_UPLOAD_FILE_H = 10,
	PARAM_TLS_STATUS = 11, /*相机状态，参考CAM_STL_STATUS_E*/
	PARAM_DSP_PWR_OFF, /*PARAM_DSP_PWR_OFF    0表示DSP已关机**/
	PARAM_DSP_LED_STATUS, /*指示灯状态**/
	PARAM_UP_OUTTIME_H, /*上传超时 给DSP上电时清零，大于零时给倒计时增时，手动清零,单位分钟*/
	PARAM_UP_OUTTIME,
	PARAM_SYNC_GPS_TIME, /*0 未获取GPS时间，相机网络同步时间后下发时间， 1 获取到GPS时间，相机不再下发时间**/
	PARAM_YEAR_H, /*RTC 年 */
	PARAM_YEAR, /*RTC 年 */
	PARAM_MONTH, /*RTC 月 十进制 1:12 表示 1:12 月**/
	PARAM_DAY, /*RTC 日 十进制 1:31*/
	PARAM_WEEK, /*RTC 周 十进制 0:6 表示周日:周六*/
	PARAM_HOUR, /*RTC 时**/
	PARAM_MINUTE, /*RTC 分**/
	PARAM_SECOND, /*RTC 秒**/
	PARAM_CAM_MODE, /*相机模式*/
	PARAM_VIDEO_LEN_H, /*视频长度 高**/
	PARAM_VIDEO_LEN, /*视频长度	*/
	PARAM_PIR_SW, /*PIR  1:ON   0:OFF*/
	PARAM_INTERVAL_HOUR, /*PIR 触发时间 小时*/
	PARAM_INTERVAL_MINUTE, /*PIR 触发时间 分钟*/
	PARAM_INTERVAL_SECOND, /*PIR 触发时间 秒钟*/
	PARAM_PIR_SENSITIVITY, /*PIR 触发灵敏度**/
	PARAM_TIMER, /*定时模式  1:ON   0:OFF*/
	PARAM_LAPSE_HOUR, /*定时触发时间间隔 小时*/
	PARAM_LAPSE_MINUTE, /*定时触发时间间隔 分钟*/
	PARAM_LAPSE_SECOND, /*定时触发时间间隔 秒钟*/
	PARAM_TIMER1_START_H, /*定时时段1 开始 小时*/
	PARAM_TIMER1_START_M, /*定时时段1 开始 分钟*/
	PARAM_TIMER1_STOP_H, /*定时时段1 结束 小时*/
	PARAM_TIMER1_STOP_M, /*定时时段1 结束 分钟*/
	PARAM_TIMER2_START_H, /*定时时段2 开始 小时*/
	PARAM_TIMER2_START_M, /*定时时段2 开始 分钟*/
	PARAM_TIMER2_STOP_H, /*定时时段2 结束 小时*/
	PARAM_TIMER2_STOP_M, /*定时时段2 结束 分钟*/
	PARAM_TIMER3_START_H, /*定时时段3 开始 小时*/
	PARAM_TIMER3_START_M, /*定时时段3 开始 分钟*/
	PARAM_TIMER3_STOP_H, /*定时时段3 结束 小时*/
	PARAM_TIMER3_STOP_M, /*定时时段3 结束 分钟*/
	PARAM_OPT_SET, /*bit0  1红外灯，0白光灯;  bit1 1 1511A   0 1511B*/
	PARAM_TIMER_WEEK_REPEAT, /*周重复**/
	PARAM_SHOT_LIMIT, /*拍摄限制*/
	PARAM_UPLOAD_NUM, /*集中上传数**/
	PARAM_NOT_UPLOAD_FILE, /*未上传文件数量**/
	PARAM_PWR_LOW_VAL_H,
	PARAM_PWR_LOW_VAL,
	PARAM_CAM_PID, /*相机的PID 4位**/
	PARAM_CAM_PID_END = PARAM_CAM_PID + 5,
	PARAM_EAX_PID, /*基站的PID 8位**/
	PARAM_EAX_PID_END = PARAM_EAX_PID + 9,
	PARAM_CAM_PWR_RESET, /*DSP 把该位置1后，断电重启设备*/

	/*基站设置参数*/
	PARAM_EAX_MODE, /*基站模式*/
	PARAM_EAX_SLEEP_START_H, /*基站节能时段 开始 小时*/
	PARAM_EAX_SLEEP_START_M, /*基站节能时段 开始 分钟*/
	PARAM_EAX_SLEEP_STOP_H, /*基站节能时段 结束 小时*/
	PARAM_EAX_SLEEP_STOP_M, /*基站节能时段 结束 分钟*/

	/*基站信息*/
	PARAM_EAX_INFO_BATTERY_A, /*电池A组电压 除10后转换成电压值 单位V*/
	PARAM_EAX_INFO_BATTERY_B, /*电池B组电压 除10后转换成电压值 单位V*/
	PARAM_EAX_INFO_PWR_OUT_V, /*基站 外接电源电压*/
	PARAM_EAX_INFO_SUN_V, /*太阳能 电源电压*/
	PARAM_EAX_INFO_WIFI_STATUS, /*WIFI状态 0 WIFI已准备好可联网**/
	PARAM_EAX_INFO_MODE, /*基站当前设置的模式**/
	PARAM_EAX_INFO_SLEEP_S_H, /*基站当前设置的节能时段 开始 小时*/
	PARAM_EAX_INFO_SLEEP_S_M, /*基站当前设置的节能时段 开始 分钟*/
	PARAM_EAX_INFO_SLEEP_E_H, /*基站当前设置的节能时段 结束 小时*/
	PARAM_EAX_INFO_SLEEP_E_M, /*基站当前设置的节能时段 结束 分钟*/
	PARAM_EAX_INFO_VERSION, /*基站当前程序版本*/

	/*基站GPS信息*/
	PARAM_EAX_GPS_INFO_LON, /*经度 字符串占用10个字节**/
	PARAM_EAX_GPS_INFO_U_LON = PARAM_EAX_GPS_INFO_LON + 11, /*经度方向：E-东，W-西**/
	PARAM_EAX_GPS_INFO_LAT, /*纬度 字符串占用10个字节**/
	PARAM_EAX_GPS_INFO_U_LAT = PARAM_EAX_GPS_INFO_LAT + 10, /*纬度方向：N-北，S-南**/
	PARAM_EAX_GPS_INFO_ALTITUDE, /*海拔高度 站字符串占用10个字节**/
	PARAM_EAX_GPS_INFO_ALTITUDE_E = PARAM_EAX_GPS_INFO_ALTITUDE + 10, /*海拔高度 字符串结速地址*/

	/*GPS信息 */
	PARAM_GPS_INFO_LON, /*经度 字符串占用10个字节**/
	PARAM_GPS_INFO_U_LON = PARAM_GPS_INFO_LON + 11, /*经度方向：E-东，W-西**/
	PARAM_GPS_INFO_LAT, /*纬度 字符串占用10个字节**/
	PARAM_GPS_INFO_U_LAT = PARAM_GPS_INFO_LAT + 10, /*纬度方向：N-北，S-南**/
	PARAM_GPS_INFO_ALTITUDE, /*海拔高度 站字符串占用10个字节*/
	PARAM_GPS_INFO_ALTITUDE_E = PARAM_GPS_INFO_ALTITUDE + 10, /*海拔高度 字符串结速地址*/

	/*模块信息*/
	PARAM_MODULAR_RSSI_L,
	PARAM_MODULAR_RSSI_H, /*信号强度测量值dBm "-141" to "-44"  "-1" 无效值**/
	PARAM_MODULAR_RSRP_L,
	PARAM_MODULAR_RSRP_H, /*dBm -141" to "-44"*/
	PARAM_MODULAR_RSRQ_L,
	PARAM_MODULAR_RSRQ_H, /*"-196" to "-30" */
	PARAM_MODULAR_EARFEN_L,
	PARAM_MODULAR_EARFEN_H,
	/* 测量结果的频点信息**/ /* CF */
	PARAM_MODULAR_DISTANCE_L,
	PARAM_MODULAR_DISTANCE_H, /* 与对端节点距离, 单位为米，取值范围[0, 5000] */
	PARAM_MODULAR_TX_POWER, /* 传输功率，单位 dBm, "-50" to "+50" */
	PARAM_MODULAR_SNR, /* "-50" to "+50" */
	PARAM_MODULAR_IP, /* 基站模块IP 4字节 */
	PARAM_MODULAR_IP_END = PARAM_MODULAR_IP + 4,
	PARAM_CENTER_NODE_PID, /* 中心节点PID 8位，占用9字节空间 */
	PARAM_CENTER_NODE_PID_END = PARAM_CENTER_NODE_PID + 9,
	PARAM_SYS_ON_TIME_L,
	PARAM_SYS_ON_TIME_H,
	PARAM_SYS_HEART_RATE_0,
	/*0-7 bit */ /* SYS_HeartRate 心跳间隔 数据类型uint32_t 取值范围 1~86400 默认值3600 */
	PARAM_SYS_HEART_RATE_1, /*8-15 bit*/
	PARAM_SYS_HEART_RATE_2, /*16-23 bit*/
	PARAM_SYS_HEART_RATE_3, /*24-31 bit*/

	PARAM_SYS_LOW_VOLTAGE_L,
	/*0-7 bit*/ /* SYS_LowVoltage 低电预警电压 数据类型uint16_t 单位mV */
	PARAM_SYS_LOW_VOLTAGE_H, /*8-15 bit*/

	PARAM_SYS_END_VOLTAGE_L,
	/*0-7 bit*/ /* SYS_EndVoltage 关机保护电压 数据类型uint16_t 单位mV */
	PARAM_SYS_END_VOLTAGE_H, /*8-15 bit*/

	PARAM_CONFIG_LOW_VOLTAGE_L,
	/*0-7 bit*/ /* SYS_LowVoltage 低电预警电压 数据类型uint16_t 单位mV */
	PARAM_CONFIG_LOW_VOLTAGE_H, /*8-15 bit*/

	PARAM_CONFIG_END_VOLTAGE_L,
	/*0-7 bit */ /* SYS_EndVoltage 关机保护电压 数据类型uint16_t 单位mV 默认值0*/
	PARAM_CONFIG_END_VOLTAGE_H, /* 8-15 bit */

	PARAM_PWR_OUT_VOLTAGE_L,
	PARAM_PWR_OUT_VOLTAGE_H, /* 相机外接电源电压 */
	PARAM_CAM_ORDINARY_MODE, /* 1 普通相机模式， 0 网络相机模式 */

	/*基站设置参数  20220513 新增*/
	PARAM_EAX_LOW_VOLTAGE, /* 基站低电预警电压 除10后转换成电压值 单位V */
	PARAM_EAX_END_VOLTAGE, /* 基站关机保护电压 除10后转换成电压值 单位V */

	PARAM_4G_EXIST, /* 检测试到4G模块时置 1*/
	PARAM_4G_WIFI_OK, /* 0 WIFI 准备好，1 WIFI未准备好 */
	PARAM_4G_RSSI, /* 4G信号强度，转换为实际的dBm -44 ~ -127 */
	PARAM_4G_ACT, /* 当前制式 0-无服务,3-GSM/GPRS 模式,4-WCDMA 模式,15-TD-SCDMA 模式,17-LTE 模式 */

	/*外部无线探测器  GPS坐标 取原来相机的GPS坐标（探测器时覆盖）*/
	PARAM_RM_ID, /*当前触发ID，0为相机本机触发*/
	PARAM_RM_ID_END = PARAM_RM_ID + 3,
	PARAM_RM_TYPE, /*类型  0 PIR探头    1 噪音  2 振动*/
	PARAM_RM_BAT_V, /*电池电压 电压值转换 126  126 / 10 = 12.6v*/
	PARAM_RM_SUN_V, /*太阳能电压 电压值转换 126  126 / 10 = 12.6v*/
	PARAM_RM_COUNT_L,
	PARAM_RM_COUNT_H, /*触发计数*/
	PARAM_RM_NOISE_L,
	PARAM_RM_NOISE_H, /*传感器的值**/
	PARAM_REMOTE_WAKE_EN = 218, /* 4G远程唤醒 0 关闭 1 开启 */
	PARAM_SHOT_CONTINUOUS = 219,
	PARAM_CAM_PID_NEW = 256, /*Camera's PID*/
	PARAM_CAM_PID_NEW_END = PARAM_CAM_PID_NEW + 20,

	PARAM_CAM_WIFI_SSID, /* wifi SSID in STA mode.*/
	PARAM_CAM_WIFI_SSID_END = PARAM_CAM_WIFI_SSID + 16,

	PARAM_CAM_WIFI_PWD, /* wifi SSID in STA mode.*/
	PARAM_CAM_WIFI_PWD_END = PARAM_CAM_WIFI_PWD + 16,
	PARAM_EVENT_TYPE, /* EVENT_TYPE_E */
	PARAM_EVENT_ID,
	PARAM_EVENT_ID_END = PARAM_EVENT_ID + 3,
	PARAM_EVENT_NUM,

	PARAM_ARR_NUM, /* 参数数组大小 放在参数的最后面 */

};

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
	unsigned char buf[2] = { 0 };
	buf[0] = (seconds >> 8) & 0xFF;
	buf[1] = seconds & 0xFF;
	int reg_start = PARAM_UNPACK_START(PARAM_UP_OUTTIME_H);
	if (iic->write(reg_start, &buf[0], 1) != 1) {
		return false;
	}
	reg_start = PARAM_UNPACK_START(PARAM_UP_OUTTIME);
	if (iic->write(reg_start, &buf[1], 1) != 1) {
		return false;
	}
	return true;
}

int MCU::readShutdownVoltage()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_SYS_END_VOLTAGE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_SYS_END_VOLTAGE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readLowPowerVoltage()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_SYS_LOW_VOLTAGE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_SYS_LOW_VOLTAGE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readBatteryLevel()
{
	return 0;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_BATTERY, buf, 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readBatteryType()
{
	return 0;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_BAT_TYPE, buf, 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

bool MCU::IsWifiStationReady()
{
	return true;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_EAX_INFO_WIFI_STATUS, buf, 1) != 1) {
		return false;
	}
	return buf[0] == 0;
}

int MCU::readSignalCF()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_MODULAR_EARFEN_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_MODULAR_EARFEN_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalRSSI()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_MODULAR_RSSI_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_MODULAR_RSSI_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalRSRP()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_MODULAR_RSRP_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_MODULAR_RSRP_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalRSRQ()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_MODULAR_RSRQ_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_MODULAR_RSRQ_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalSNR()
{
	return 0;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_MODULAR_SNR, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readSignalTD()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_MODULAR_DISTANCE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_MODULAR_DISTANCE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalTP()
{
	return 0;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_MODULAR_TX_POWER, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

bool MCU::Is4gExist()
{
	return false;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_4G_EXIST, buf, 1) != 1) {
		return false;
	}
	value = buf[0];
	return value == 1;
}

bool MCU::writeRemoteWakeup(int remote_wakeup)
{
	return true;
	unsigned char buf[1] = { static_cast<unsigned char>(remote_wakeup) };
	int reg_start = PARAM_UNPACK_START(PARAM_REMOTE_WAKE_EN);
	if (iic->write(reg_start, buf, 1) != 1) {
		return false;
	}
	return true;
}

bool MCU::useGpsTime()
{
	return false;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_SYNC_GPS_TIME, buf, 1) != 1) {
		return false;
	}
	value = buf[0];
	return value == 1;
}

int MCU::readCds()
{
	return 0;
	int cds = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_IR_VALUE_H, &buf[0], 1) != 1) {
		return 0;
	}

	if (iic->read(PARAM_IR_VALUE, &buf[1], 1) != 1) {
		return 0;
	}

	cds = (buf[0] << 8) | buf[1];
	return cds;
}

bool MCU::IsRemoteWakeup()
{
	return false;
	unsigned char buf = 0;
	if (iic->read(PARAM_STATUS, &buf, 1) != 1) {
		return false;
	}
	return (buf & 0x10) ? true : false;
}

int MCU::readRMID()
{
	return 0;
	int idx = 0;
    uint32_t sensorID = 0;
	uint8_t *pID = (uint8_t *)&sensorID;

	for (idx = 0; idx < 4; idx++) {
		unsigned char buf[1] = { 0 };
		if (iic->read(PARAM_RM_ID + idx, &buf[0], 1) != 1) {
			return 0;
		}
		pID[idx]= buf[0];
	}

	return sensorID;
}

int MCU::readRMType()
{
	return 0;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_RM_TYPE, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readRMValue()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_RM_NOISE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_RM_NOISE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readRMCount()
{
	return 0;
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (iic->read(PARAM_RM_COUNT_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (iic->read(PARAM_RM_COUNT_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}
int MCU::readEventType()
{
	return 0;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_EVENT_TYPE, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readEventID()
{
	return 0;
	int idx = 0;
    int eventID = 0;
	uint8_t *pID = (uint8_t *)&eventID;

	for (idx = 0; idx < 4; idx++) {
		unsigned char buf[1] = { 0 };
		if (iic->read(PARAM_EVENT_ID + idx, &buf[0], 1) != 1) {
			return 0;
		}
		pID[idx]= buf[0];
	}

	return eventID;
}

int MCU::readEventNum()
{
	return 0;
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (iic->read(PARAM_EVENT_NUM, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int MCU::readWorkingMode()
{
	int value = 0;
	char *pval = (char *)&value;
	unsigned char buf[128] = { 0 };
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_WORK_MODE);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_WORK_MODE);

	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
		Logger::log(LogLevel::ERROR, "[MCU]read working modefailed");
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_TEMPER);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_TEMPER);
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_RHS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_RHS);
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_APS);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_APS);
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
	if (iic->read(reg_start, &buf[0], nbytes) <= 0) {
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_BATTERY1);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_BATTERY1);
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_BATTERY1);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_BATTERY1);
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_BATTERY2);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_BATTERY2);
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_SPOWER);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SPOWER);
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
	int reg_start = PARAM_UNPACK_START(PARAM_MCU_EPOWER);
	int nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_EPOWER);
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
	int nbytes = 0;

	{//year
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_YEAR);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_YEAR, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read year from MCU");
			return time;
		}
		int year = 0;
		memcpy(&year, buf, nbytes);
		time.tm_year = year - YEAR_OFFSET;
	}

	{//month
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_MONTH);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_MONTH, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read month from MCU");
			return time;
		}
		int month = 0;
		memcpy(&month, buf, nbytes);
		time.tm_mon = month - MONTH_OFFSET;
	}

	{//day
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_DAY);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_DAY, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read day from MCU");
			return time;
		}
		int day = 0;
		memcpy(&day, buf, nbytes);
		time.tm_mday = day;
	}

	{//hour
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_HOUR);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_HOUR, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read hour from MCU");
			return time;
		}
		int hour = 0;
		memcpy(&hour, buf, nbytes);
		time.tm_hour = hour;
	}

	{//minute
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_MINUTE);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_MINUTE, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read minute from MCU");
			return time;
		}
		int minute = 0;
		memcpy(&minute, buf, nbytes);
		time.tm_min = minute;
	}

	{//second
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_SECOND);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_SECOND, buf, nbytes) <= 0) {
			Logger::log(LogLevel::ERROR, "Failed to read second from MCU");
			return time;
		}
		int second = 0;
		memcpy(&second, buf, nbytes);
		time.tm_sec = second;
	}

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

    //longitude
    {
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_GPSL);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_GPSL, buf, nbytes) <= 0 && buf[0] != 0) {
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

    // read latitude data
	{
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_GPSA);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_GPSL, buf, nbytes) <= 0 && buf[0] != 0) {
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

    // altitude
	{
		nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_GPSH);
		memset(buf, 0, sizeof(buf));
		if (iic->read(PARAM_MCU_GPSH, buf, nbytes) <= 0 && buf[0] != 0) {
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
			nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_GPSL);
			memset(buf, 0, sizeof(buf));
			memcpy(buf, &longitude, nbytes);
			if (iic->write(PARAM_UNPACK_START(PARAM_MCU_GPSL), buf, nbytes) <= 0) {
				Logger::log(LogLevel::ERROR, "Failed to write longitude to MCU");
				success = false;
			}
		}
		
		// 写入纬度数据
		if (success) {
			nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_GPSA);
			memset(buf, 0, sizeof(buf));
			memcpy(buf, &latitude, nbytes);
			// 注意：这里读取使用的是PARAM_MCU_GPSL地址，但根据函数名应该写入到PARAM_MCU_GPSA
			// 为保持一致性，这里使用与readGps相同的地址
			if (iic->write(PARAM_UNPACK_START(PARAM_MCU_GPSL), buf, nbytes) <= 0) {
				Logger::log(LogLevel::ERROR, "Failed to write latitude to MCU");
				success = false;
			}
		}
		
		// 写入高度数据
		if (success) {
			nbytes = PARAM_UNPACK_BYTES(PARAM_MCU_GPSH);
			memset(buf, 0, sizeof(buf));
			memcpy(buf, &altitude, nbytes);
			if (iic->write(PARAM_UNPACK_START(PARAM_MCU_GPSH), buf, nbytes) <= 0) {
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