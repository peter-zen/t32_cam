#ifndef COMMON_H
#define COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

//device config
#define INI_SECTION_DEVICE "DEVICE"
#define INI_KEY_PID "PID"
#define INI_KEY_CSSID "CSSID"
#define INI_KEY_CPWD "CPWD"
#define INI_KEY_SPKVOL "SPKVOL"
#define INI_KEY_ETH_MAC "ETH_MAC"

#define INI_SECTION_BOOT "BOOT"
#define INI_KEY_PTYPE "PType"
#define INI_KEY_PMODEL "PModel"
#define INI_KEY_PNAME "PName"
#define INI_KEY_PCOMPANT "PCompany"
#define INI_KEY_WLED "WLED"
#define INI_KEY_PLAN "PLan"
#define INI_KEY_MVIDEO "MVideo"
#define INI_KEY_MPIC "MPic"
#define INI_KEY_SMODE "SMode"
#define INI_KEY_PMAC "PMac"

#define INI_SECTION_SERVER "SERVER"
#define INI_KEY_MS_HOST "HOST"
#define INI_KEY_MS_IP "MS"
#define INI_KEY_MS_PORT "MSPort"

#define INI_KEY_NTP_IP "NTP"
#define INI_KEY_NTP_PORT "NTPPort"

#define INI_KEY_FS_IP "FS"
#define INI_KEY_FS_PORT "FSPort"

#define INI_SECTION_SYS "SYSTEM"
#define INI_KEY_UPID "UPID"
#define INI_KEY_UPWD "PWD"

#define INI_KEY_LOW_VOL "LowVoltage"
#define INI_KEY_END_VOL "EndVoltage"
#define INI_KEY_BITRATE_4K "BR4K"
#define INI_KEY_BITRATE_1080P "BR1080P"
#define INI_KEY_BITRATE_720P "BR720P"



#define INI_CFG_FILENAME "B:/UDF/OPTIONS.CFG"

#define INI_SECTION_NTP "NTP"
//#define INI_KEY_LOCATE "LOCATE"
#define INI_KEY_TIMEZONE "TIMEZONE"
//#define INI_KEY_DAYLIGHT "DAYLIGHT"

#define INI_SECTION_POLICY "POLICY"
#define INI_KEY_FILE_MANAGE "FileManage"
#define INI_KEY_REMOTE_WAKEUP "RemoteWakeup"
#define INI_KEY_RECORD "Record"

//program type
#define PTYPE_NO_NET 0
#define PTYPE_WIFI	1
#define PTYPE_USB_DONGLE	4
#define PTYPE_ETHERNET	8

//disk
#define DISK_PATHNAME "/mnt/sdcard/"

//error code
enum error_code {
	EC_SUCCESS = 0,
	EC_FAILED = -1,
	EC_TIMEOUT = -2,
	EC_INVALID_PARAM = -3,
	EC_INVALID_STATE = -4,
	EC_OPEN_FILE_FAILED = -5,
	EC_UPLOAD_FILE_FAILED = -6,
	EC_SNAP_FAILED = -7,
};

enum message_type {
	MSG_TYPE_AUTH = 0, /*author,get cuid,edui from server*/
	MSG_TYPE_UPLOAD_FILE = 1, /*upload file*/
	MSG_TYPE_DOWNLOAD_FILE = 2, /*download file*/
	MSG_TYPE_SETTING = 3, /*sync param*/
	MSG_TYPE_CMD = 4, /*send cmd to devide*/
	MSG_TYPE_RTMP = 6, /*rtmp*/

	MSG_TYPE_VOICE_BROADCAST_MARK = 98,
	MSG_TYPE_FIRMWARE_MARK = 99,

	MSG_TYPE_PUT_ALARM = 100, /*push alarm*/
	MSG_TYPE_FILE_LIST = 101, /*get voice list*/
	MSG_TYPE_SETTING_MARK = 102, /*reset setting change mark*/
	CK_MSG_TYPE_MAX = 103,
	MSG_TYPE_UPLOAD_JSON = 254,
};

/*remote ctrl enum*/
enum rc_msg_type {
	MSG_RC_GET_PARAM_ALL = 0, /*get all params*/
	MSG_RC_NOTIFY_CAP, /*notify cap*/
	MSG_RC_NOTIFY_REC, /*notify rec*/
	MSG_RC_SET_PARAM, /*set params*/
	MSG_RC_RESET_PARAM, /*reset params*/
	MSG_RC_SET_DATETIME, /*set datetime*/
	MSG_RC_ENTER_WORK_MODE, /*enter work mode*/
	MSG_RC_FORMAT_SDCARD, /*format sdcard*/
	MSG_RC_GET_SENSOR_INFO, /*get sensor info*/
	MSG_RC_GET_HW_INFO, /*get hw info*/
	MSG_RC_NOTIFY_PV, /*notify pv*/
	MSG_RC_TYPE_MAX,
};
enum dev_type {
	DEV_CAMERA = 1,
	DEV_RELAYING,
	DEV_ACCESS_INTERNET,
	DEV_ROUTER,

};

enum {
	SNAP_IMG_SIZE_2M,	
	SNAP_IMG_SIZE_4M,
	SNAP_IMG_SIZE_5M,
	SNAP_IMG_SIZE_8M,
	SNAP_IMG_SIZE_12M,
	SNAP_IMG_SIZE_18M,
	SNAP_IMG_SIZE_24M,
	SNAP_IMG_SIZE_32M,
	SNAP_IMG_SIZE_42M,
	SNAP_IMG_SIZE_MAX,
};
enum {
	VIDEO_SIZE_HD_30FPS,
	VIDEO_SIZE_HD_60FPS,
	VIDEO_SIZE_FHD_30FPS,
	VIDEO_SIZE_FHD_60FPS,
	VIDEO_SIZE_2K_30FPS,
	VIDEO_SIZE_4K2K_30FPS,
	VIDEO_SIZE_MAX,
};

static struct {
	uint16_t width,height;
} SnapImgSize[SNAP_IMG_SIZE_MAX]= {
	{1920,1080}, /* 2M */
	{2560,1440}, /* 4M */
	{2592,1944}, /* 5M */
	{3840,2160}, /* 8M */
	{4800,2700}, /* 12M */
	{5760,3240}, /* 18M */
	{6400,3600}, /* 24M */
	{7680,4320}, /* 32M */
};

//pin definition
#define PA(x)  (0 * 32 + (x))  // Port A
#define PB(x)  (1 * 32 + (x))  // Port B
#define PC(x)  (2 * 32 + (x))  // Port C

//working mode check pins
#define WORKING_MODE_CHECK_PIN_0  PC(9)
#define WORKING_MODE_CHECK_PIN_1  PC(8)

//power hold pin
#define POWER_HOLD_PIN  PB(15)

//CDS sensor pin
#define CDS_SENSOR_PIN  PA(10)

//IR-CUT pins
#define IR_CUT_ENABLE_PIN    PB(13)
#define IR_CUT_CTRL_PIN    PB(14)

//IR LED pin
#define IR_LED_PIN      PB(10)

//RGB LED pins
#define RGB_LED_PIN   PB(11)


//DATE TIME
#define YEAR_MIN 2000
#define YEAR_OFFSET 1900
#define MONTH_OFFSET 1

#ifdef __cplusplus
}
#endif
#endif
