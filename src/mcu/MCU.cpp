#include "MCU.h"
#include <mutex>
#include "Logger.h"
#include "RTC.h"
#include "StringConvert.h"
#include "Common.h"

#define I2C_BUS_NAME "/dev/i2c-0"
#define I2C_SLAVE_ADDRESS "0x50"

enum {
	PARAM_STATUS = 1, /*工作状，参考STATUS_BIT_E*/
	PARAM_IR_VALUE_H, /*光敏值 高8bit*/
	PARAM_IR_VALUE, /*光敏值 低8bit*/
	PARAM_BATTERY, /*电池电量等级0-3，3最高，0 为电池电量低，1-3代表电池电量低中高3个等级**/
	PARAM_BAT_H, /*电池电量电压值 高8bit*/
	PARAM_BAT_L, /*电池电量电压值 低8bit*/
	PARAM_TEMPER, /*热敏电阻值0-216，减40后转换为实际的华氏度值，摄氏度测量范围（-40C ~ 80C）**/
	PARAM_BAT_TYPE, /*电池类型BATTERY_TYPE_E，根据电池电压来区分，上电时大于8.5V为干电池；0 18650理电池，1 1.5V干电池，暂不能匹配1.2V电池*/
	PARAM_MCU_VERSION, /*DSP需转换成字符串上传给遥控器，例：100 转换为"100"*/

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
{
	i2c = std::make_shared<I2C>(I2C_BUS_NAME, I2C_SLAVE_ADDRESS);
}

MCU::~MCU()
{
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
	unsigned char buf[2] = { 0 };
	buf[0] = (seconds >> 8) & 0xFF;
	buf[1] = seconds & 0xFF;
	if (i2c->write(PARAM_UP_OUTTIME_H, &buf[0], 1) != 1) {
		return false;
	}
	if (i2c->write(PARAM_UP_OUTTIME, &buf[1], 1) != 1) {
		return false;
	}
	return true;
}

int MCU::readBatteryVoltage()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_BAT_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_BAT_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readExternalVoltage()
{
	int value = 0;
	unsigned char buf[2] = { 0 };

	if (i2c->read(PARAM_PWR_OUT_VOLTAGE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_PWR_OUT_VOLTAGE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	if (value <= 5000) {
		value = 0;
	}
	return value;
}

int MCU::readShutdownVoltage()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_SYS_END_VOLTAGE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_SYS_END_VOLTAGE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readLowPowerVoltage()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_SYS_LOW_VOLTAGE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_SYS_LOW_VOLTAGE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readBatteryLevel()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_BATTERY, buf, 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readBatteryType()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_BAT_TYPE, buf, 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readTemperature()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_TEMPER, buf, 1) != 1) {
		return 0;
	}
	value = buf[0];
	value -= 40;
	value = 10 * 5 * (value - 32) / 9;
	return value;
}

bool MCU::IsWifiStationReady()
{
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_EAX_INFO_WIFI_STATUS, buf, 1) != 1) {
		return false;
	}
	return buf[0] == 0;
}

std::string MCU::readGps()
{
    static std::string cached_data;

    // if cached gps data, return it
    if (!cached_data.empty()) {
        return cached_data;
    }

    std::string longitude;
    std::string latitude;
    std::string altitude;
    char longitude_direction = 0;
    char latitude_direction = 0;

    // read longitude data
    for (int i = 0; i < 11; i++) {
        unsigned char buf[1];
        if (i2c->read(PARAM_EAX_GPS_INFO_LON + i, buf, 1) == 1) {
            if (buf[0] != 0)
                longitude.push_back(static_cast<char>(buf[0]));
        }
    }

    // longitude is empty, return empty gps data
    if (longitude.empty()) {
        cached_data = ",,,,,";
        return cached_data;
    }

    // read longitude direction
    unsigned char dir_buf[1];
    if (i2c->read(PARAM_EAX_GPS_INFO_U_LON, dir_buf, 1) == 1) {
        longitude_direction = static_cast<char>(dir_buf[0]);
    }
    if (!longitude_direction) {
        cached_data = ",,,,,";
        return cached_data;
    }

    // read latitude data
    for (int i = 0; i < 10; i++) {
        unsigned char buf[1];
        if (i2c->read(PARAM_EAX_GPS_INFO_LAT + i, buf, 1) == 1) {
            if (buf[0] != 0)
                latitude.push_back(static_cast<char>(buf[0]));
        }
    }

    // latitude is empty, return empty gps data
    if (latitude.empty()) {
        cached_data = ",,,,,";
        return cached_data;
    }

    // read latitude direction
    if (i2c->read(PARAM_EAX_GPS_INFO_U_LAT, dir_buf, 1) == 1) {
        latitude_direction = static_cast<char>(dir_buf[0]);
    }
    if (!latitude_direction) {
        cached_data = ",,,,,";
        return cached_data;
    }

    // read altitude data
    for (int i = 0; i < 10; i++) {
        unsigned char buf[1];
        if (i2c->read(PARAM_EAX_GPS_INFO_ALTITUDE + i, buf, 1) == 1) {
            if (buf[0] != 0)
                altitude.push_back(static_cast<char>(buf[0]));
        }
    }

    // assemble gps data string
    if (!altitude.empty()) {
        cached_data = longitude + "," + longitude_direction + "," + latitude + "," + latitude_direction + "," +
                      altitude;
    } else {
        cached_data = ",,,,,";
    }

    return cached_data;
}

bool MCU::writeGps(const std::string &gps)
{
	//TODO: write gps string
	return true;
}

int MCU::readSignalCF()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_MODULAR_EARFEN_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_MODULAR_EARFEN_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalRSSI()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_MODULAR_RSSI_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_MODULAR_RSSI_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalRSRP()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_MODULAR_RSRP_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_MODULAR_RSRP_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalRSRQ()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_MODULAR_RSRQ_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_MODULAR_RSRQ_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalSNR()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_MODULAR_SNR, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readSignalTD()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_MODULAR_DISTANCE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_MODULAR_DISTANCE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readSignalTP()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_MODULAR_TX_POWER, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

bool MCU::Is4gExist()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_4G_EXIST, buf, 1) != 1) {
		return false;
	}
	value = buf[0];
	return value == 1;
}

bool MCU::writeRemoteWakeup(int remote_wakeup)
{
	unsigned char buf[1] = { static_cast<unsigned char>(remote_wakeup) };
	if (i2c->write(PARAM_REMOTE_WAKE_EN, buf, 1) != 1) {
		return false;
	}
	return true;
}

bool MCU::setDatetime(const struct tm &time)
{
	Logger::log(LogLevel::INFO, "%04d-%02d-%02d %02d:%02d:%02d", time.tm_year + YEAR_OFFSET, time.tm_mon + MONTH_OFFSET, time.tm_mday,
		    time.tm_hour, time.tm_min, time.tm_sec);

	unsigned char buf[1];

	buf[0] = (time.tm_year + YEAR_OFFSET) / 100;
	if (i2c->write(PARAM_YEAR_H, buf, 1) != 1)
		return false;

	buf[0] = (time.tm_year + YEAR_OFFSET) % 100;
	if (i2c->write(PARAM_YEAR, buf, 1) != 1)
		return false;

	buf[0] = time.tm_mon + MONTH_OFFSET;
	if (i2c->write(PARAM_MONTH, buf, 1) != 1)
		return false;

	buf[0] = time.tm_mday;
	if (i2c->write(PARAM_DAY, buf, 1) != 1)
		return false;

	buf[0] = time.tm_wday;
	if (i2c->write(PARAM_WEEK, buf, 1) != 1)
		return false;

	buf[0] = time.tm_hour;
	if (i2c->write(PARAM_HOUR, buf, 1) != 1)
		return false;

	buf[0] = time.tm_min;
	if (i2c->write(PARAM_MINUTE, buf, 1) != 1)
		return false;

	buf[0] = time.tm_sec;
	if (i2c->write(PARAM_SECOND, buf, 1) != 1)
		return false;

	return true;
}

struct tm MCU::getDatetime()
{
	struct tm time_info = {};
	unsigned char buf[1] = { 0 };

	// 读取年份高位和低位
	if (i2c->read(PARAM_YEAR_H, buf, 1) != 1) {
		return time_info;
	}
	int year_h = buf[0];

	if (i2c->read(PARAM_YEAR, buf, 1) != 1) {
		return time_info;
	}
	int year = year_h * 100 + buf[0];

	// 读取月份
	if (i2c->read(PARAM_MONTH, buf, 1) != 1) {
		return time_info;
	}
	int month = buf[0];

	// 读取日期
	if (i2c->read(PARAM_DAY, buf, 1) != 1) {
		return time_info;
	}
	int day = buf[0];

	// 读取小时
	if (i2c->read(PARAM_HOUR, buf, 1) != 1) {
		return time_info;
	}
	int hour = buf[0];

	// 读取分钟
	if (i2c->read(PARAM_MINUTE, buf, 1) != 1) {
		return time_info;
	}
	int minute = buf[0];

	// 读取秒钟
	if (i2c->read(PARAM_SECOND, buf, 1) != 1) {
		return time_info;
	}
	int second = buf[0];

	Logger::log(LogLevel::INFO, "%04d/%02d/%02d %02d:%02d:%02d", year, month, day, hour, minute, second);

	/* 检查时间值是否有效 */
	if ((second >= 0 && second < 60) && (minute >= 0 && minute < 60) && (hour >= 0 && hour < 24) &&
	    (day >= 1 && day <= 31) && (month >= 1 && month <= 12) && ((year % 100) >= 23) && ((year % 100) <= 99)) {
		/* 转换为tm结构 */
		time_info.tm_year = year - YEAR_OFFSET;
		time_info.tm_mon = month - MONTH_OFFSET;
		time_info.tm_mday = day;
		time_info.tm_hour = hour;
		time_info.tm_min = minute;
		time_info.tm_sec = second;
	}

	return time_info;
}

bool MCU::useGpsTime()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_SYNC_GPS_TIME, buf, 1) != 1) {
		return false;
	}
	value = buf[0];
	return value == 1;
}

int MCU::readCds()
{
	int cds = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_IR_VALUE_H, &buf[0], 1) != 1) {
		return 0;
	}

	if (i2c->read(PARAM_IR_VALUE, &buf[1], 1) != 1) {
		return 0;
	}

	cds = (buf[0] << 8) | buf[1];
	return cds;
}

std::string MCU::readVersion()
{
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_MCU_VERSION, buf, 1) != 1) {
		return "";
	}
	return to_string_custom(buf[0]);
}

bool MCU::IsRemoteWakeup()
{
	unsigned char buf = 0;
	if (i2c->read(PARAM_STATUS, &buf, 1) != 1) {
		return false;
	}
	return (buf & 0x10) ? true : false;
}

int MCU::readRMID()
{
	int idx = 0;
    uint32_t sensorID = 0;
	uint8_t *pID = (uint8_t *)&sensorID;

	for (idx = 0; idx < 4; idx++) {
		unsigned char buf[1] = { 0 };
		if (i2c->read(PARAM_RM_ID + idx, &buf[0], 1) != 1) {
			return 0;
		}
		pID[idx]= buf[0];
	}

	return sensorID;
}

int MCU::readRMType()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_RM_TYPE, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readRMValue()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_RM_NOISE_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_RM_NOISE_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}

int MCU::readRMBatteryValue()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_RM_BAT_V, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readRMSunPowerValue()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_RM_SUN_V, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readRMCount()
{
	int value = 0;
	unsigned char buf[2] = { 0 };
	if (i2c->read(PARAM_RM_COUNT_H, &buf[0], 1) != 1) {
		return 0;
	}
	if (i2c->read(PARAM_RM_COUNT_L, &buf[1], 1) != 1) {
		return 0;
	}
	value = (buf[0] << 8) | buf[1];
	return value;
}
int MCU::readEventType()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_EVENT_TYPE, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

int MCU::readEventID()
{
	int idx = 0;
    int eventID = 0;
	uint8_t *pID = (uint8_t *)&eventID;

	for (idx = 0; idx < 4; idx++) {
		unsigned char buf[1] = { 0 };
		if (i2c->read(PARAM_EVENT_ID + idx, &buf[0], 1) != 1) {
			return 0;
		}
		pID[idx]= buf[0];
	}

	return eventID;
}

int MCU::readEventNum()
{
	int value = 0;
	unsigned char buf[1] = { 0 };
	if (i2c->read(PARAM_EVENT_NUM, &buf[0], 1) != 1) {
		return 0;
	}
	value = buf[0];
	return value;
}

