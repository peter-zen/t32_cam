#ifndef SETTINGS_H
#define SETTINGS_H

#include <memory>
#include <string>
#include <fstream>
#include <stdint.h>
#include <json/json.h>

#include "../../common/Common.h"


class Settings {
public:
	static std::shared_ptr<Settings> getInstance();

	/**
	 * @brief Save all settings to a JSON file
	 * @param filePath Path to the JSON file
	 * @return true if successful, false otherwise
	 */
	bool saveToJsonFile(const std::string& filePath);

	/**
	 * @brief Load settings from a JSON file
	 * @param filePath Path to the JSON file
	 * @return true if successful, false otherwise
	 */
	bool loadFromJsonFile(const std::string& filePath);

public:
	uint8_t stillSize = 1;
	uint8_t stillQuality;
	uint8_t stillDriverMode;
	uint8_t stillStamp;

	uint8_t videoSize = VIDEO_SIZE_FHD_30FPS;
	uint8_t videoQuality;
	uint8_t videoSeamless;
	uint8_t videoStamp;
	uint8_t videoCodec = 1;   // 1=H.264, 2=H.265 (registry value, needs -1 for VideoCodecFormat enum)
	uint8_t videoRcMode = 1;  // 1=CBR, 2=VBR, 3=CVBR, 4=SMART (registry value)
	uint8_t audioRecordVolume = 80;
	uint8_t audioRecordGain = 28;

	uint8_t metering;
	uint8_t wb;

	uint8_t iso;
	uint8_t ev;
	uint8_t edge;
	uint8_t stablizer;
	uint8_t pvflickermode;

	uint8_t viddist;
	uint8_t vidrsc;

	/*--- ckvison ---*/

	uint8_t cameraMode;
	uint8_t realCameraMode;
	uint8_t burstNumber=1;
	uint8_t shootingLimits;
	uint8_t shootingInterval = 1;  // unit: 100ms (1=100ms, range 1-20 mapping 100ms-2000ms)
	uint8_t videoLength_h;
	uint8_t videoLength_l = 30;

	uint8_t pirEn = 1;
	uint8_t ckPirSensitivity = 2;
	uint8_t trigInterval_h;
	uint8_t trigInterval_m;
	uint8_t trigInterval_s;

	uint8_t timerEn;
	uint8_t timerLapse_h;
	uint8_t timerLapse_m;
	uint8_t timerLapse_s;

	uint8_t timer1s_h;
	uint8_t timer1s_m;
	uint8_t timer1e_h;
	uint8_t timer1e_m;

	uint8_t timer2s_h;
	uint8_t timer2s_m;
	uint8_t timer2e_h;
	uint8_t timer2e_m;

	uint8_t timer3s_h;
	uint8_t timer3s_m;
	uint8_t timer3e_h;
	uint8_t timer3e_m;

	uint8_t weekRepeats;
	uint8_t stampEn = 1;
	uint8_t autoCover = 1;

	uint8_t showDevNameEn;
	uint8_t pwdEn;

	uint8_t onTime_0;
	uint8_t onTime_1;

	uint8_t heartRate_0;
	uint8_t heartRate_1;
	uint8_t heartRate_2;
	uint8_t heartRate_3;

	uint8_t lowVol_l;
	uint8_t lowVol_h;

	uint8_t endVol_l;
	uint8_t endVol_h;

	uint8_t uploadCnt;

	uint8_t gpsLatitudeRef;
	uint8_t gpsLatitude_0;
	uint8_t gpsLatitude_1;
	uint8_t gpsLatitude_2;
	uint8_t gpsLatitude_3;

	uint8_t gpsLongitudeRef;
	uint8_t gpsLongitude_0;
	uint8_t gpsLongitude_1;
	uint8_t gpsLongitude_2;
	uint8_t gpsLongitude_3;

	uint8_t gpsAltitudeRef;
	uint8_t gpsAltitude_0;
	uint8_t gpsAltitude_1;
	uint8_t gpsAltitude_2;
	uint8_t gpsAltitude_3;

	char devName[16];
	char devPwd[4];

	uint8_t bitRate_4k = 8;       // 2026-06-10: 32→8 (mapping table: 4K=8Mbps, 见 doc/knowledge/bugs/T32-crc-fix-verification-2026-06-10.md)
	uint8_t bitRate_2_5k = 6;     // 2026-06-10: 新增 2.5K (2560x1440) 桶, 6 Mbps
	uint8_t bitRate_1080p = 4;    // 2026-06-10: 16→4 (mapping table: 1080P=4Mbps)
	uint8_t bitRate_720p = 2;     // 2026-06-10: 8→2 (mapping table: 720P=2Mbps)
	uint8_t isWLed;
	uint8_t continuous_record;
	uint8_t remote_wakeup;
	/* string variables */
	std::string comm_code;
	std::string euid;
	std::string duid;
	// T25 Phase-3: user-mutable fields migrated from config.json SYSTEM/NTP section
	std::string timezone {"UTC+8"};   // NTP.TIMEZONE (UTC-prefixed for Timezone::setTimezone)
	std::string upid;                 // SYSTEM.UPID (user WiFi SSID)
	std::string upwd;                 // SYSTEM.PWD  (user WiFi password)
	std::string lowVoltage {"0.0"};   // SYSTEM.LowVoltage (conservative string path, §12.3)
	std::string endVoltage {"0.0"};   // SYSTEM.EndVoltage
	/* int variables */
	int setting_mark;
	int enable_firmware_update;
	int force_upload;//0: no force upload, 1: force upload
private:
	Settings(const Settings &) = delete;
	Settings &operator=(const Settings &) = delete;
	Settings() = default;
};

#endif
