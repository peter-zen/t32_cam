#include <string.h>
#include <sstream>
#include <algorithm>
#include <sys/time.h>
#include "RemoteCtrlClient.h"
#include "Common.h"
#include "Logger.h"
#include "MCU.h"
#include "Usb4gDongle.h"
#include "Settings.h"
#include "DeviceConfig.h"
#include "RTC.h"
#include "Power.h"
#include "Disk.h"
#include "StringConvert.h"

using namespace network;
RemoteCtrlClient::RemoteCtrlClient(const std::string &address, int port)
	: Client(address, port), remote_is_mobile_app(true)
{

}

RemoteCtrlClient::~RemoteCtrlClient()
{
}

void RemoteCtrlClient::receiveFunction()
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Recv data");

	while (recv_thread_run) {
		fd_set read_fds;
		struct timeval timeout;
		FD_ZERO(&read_fds);
		FD_SET(socket_fd, &read_fds);
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		if (select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) {
			continue;
		}

		if (!FD_ISSET(socket_fd, &read_fds)) {
			continue;
		}

		memset(recv_buffer.get(), 0, recv_buffer_size);
		int ret = receiveCommand(recv_buffer.get(), recv_buffer_size);
		if (ret > 0) {
			processCommand(recv_buffer, ret);
		} else if (ret <= 0) {
			Logger::log(LogLevel::WARNING, "Remote controller disconnect, retrying connection for 120s");
			if (connect(120) == -1) {
				Logger::log(LogLevel::ERROR, "Retry connection error");
				is_connected = false;
				break;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}
	recv_thread_run = false;
}

int RemoteCtrlClient::receiveCommand(char *buffer, size_t length)
{
	return receive(buffer, length, 0);
}

void RemoteCtrlClient::processCommand(std::unique_ptr<char[]> &buffer, size_t length)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Process command");

	int total_length = 0;
	int type = 0;
	int msg_length = 0;

	memcpy(&total_length, buffer.get(), 4);
	memcpy(&type, buffer.get() + 4, 4);
	memcpy(&msg_length, buffer.get() + 8, 4);
	const char *payload = buffer.get() + 12;

	Logger::log(LogLevel::INFO, "Total: %d, Type: 0x%x, MsgLength: %d", total_length, type, msg_length);

	Json::CharReaderBuilder reader;
	Json::Value root;
	std::string errs;
	std::istringstream s(payload);
	if (!Json::parseFromStream(reader, s, &root, &errs)) {
		Logger::log(LogLevel::ERROR, "Failed to parse JSON: %s", errs.c_str());
		return;
	}

	switch (type) {
	case MSG_RC_GET_PARAM_ALL:
		handleGetParamAllCommand(root);
		break;
	case MSG_RC_SET_PARAM:
		handleSetParamCommand(root);
		break;
	case MSG_RC_RESET_PARAM:
		handleResetParamCommand(root);
		break;
	case MSG_RC_SET_DATETIME:
		handleSetDatetimeCommand(root);
		break;
	case MSG_RC_ENTER_WORK_MODE:
		handleEnterWorkingModeCommand(root);
		break;
	case MSG_RC_GET_SENSOR_INFO:
		handleGetSensorInfoCommand(root);
		break;
	case MSG_RC_GET_HW_INFO:
		handleGetHwInfoCommand(root);
		break;
	case MSG_RC_FORMAT_SDCARD:
		handleFormatSdcardCommand(root);
		break;
	default:
		Logger::log(LogLevel::WARNING, "Unknown remote ctrl message type: %d", type);
		break;
	}
}

void RemoteCtrlClient::handleGetParamAllCommand(const Json::Value &root)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle get param all command");
	auto settings = Settings::getInstance();
	std::string buffer;
	Json::Value json_obj;
	Json::Value json_item;
	Json::Value json_item_sub;
	Json::Value json_array(Json::arrayValue);

	int val;

	json_item["ENV_SD"] = "";
	json_item["ENV_SP"] = "";
	json_item["ENV_S"] = "";
	json_item["ENV_HT"] = "";
	json_item["ENV_IT"] = "";
	json_item["ENV_II"] = "";
	vid_max_size = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_MVIDEO, 8);
	pic_max_size = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_MPIC, 12);
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: vid_max_size: %d, pic_max_size: %d", vid_max_size, pic_max_size);
	// Program Type
	json_item["Program_Type"] = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0);

	// Shooting mode
	json_item["CAM_Mode"] = settings->realCameraMode;
	if (pic_max_size != 0) {
		// Image size
		uint8_t still_size = settings->stillSize;
		if (pic_max_size == 32 || pic_max_size == 42) {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");
			json_array.append("5M/2592*1944");
			json_array.append("8M/3840*2160");
			json_array.append("12M/4800*2700");
			json_array.append("18M/5760*3240");
			json_array.append("24M/6400*3600");
			json_array.append("32M/7680*4320");
			json_array.append("42M/8640*4864");
			if (still_size > SNAP_IMG_SIZE_42M) {
				still_size = SNAP_IMG_SIZE_42M;
			}
		} else if (pic_max_size == 24) {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");
			json_array.append("5M/2592*1944");
			json_array.append("8M/3840*2160");
			json_array.append("12M/4800*2700");
			json_array.append("18M/5760*3240");
			json_array.append("24M/6400*3600");
			if (still_size > SNAP_IMG_SIZE_24M) {
				still_size = SNAP_IMG_SIZE_24M;
			}
		} else if (18 == pic_max_size) {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");
			json_array.append("5M/2592*1944");
			json_array.append("8M/3840*2160");
			json_array.append("12M/4800*2700");
			json_array.append("18M/5760*3240");

			if (still_size > SNAP_IMG_SIZE_18M) {
				still_size = SNAP_IMG_SIZE_18M;
			}
		} else if (12 == pic_max_size) {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");
			json_array.append("5M/2592*1944");
			json_array.append("8M/3840*2160");
			json_array.append("12M/4800*2700");

			if (still_size > SNAP_IMG_SIZE_12M) {
				still_size = SNAP_IMG_SIZE_12M;
			}
		} else if (8 == pic_max_size) {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");
			json_array.append("5M/2592*1944");
			json_array.append("8M/3840*2160");

			if (still_size > SNAP_IMG_SIZE_8M) {
				still_size = SNAP_IMG_SIZE_8M;
			}
		}
		else if (5 == pic_max_size) {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");
			json_array.append("5M/2592*1944");

			if (still_size > SNAP_IMG_SIZE_5M) {
				still_size = SNAP_IMG_SIZE_5M;
			}
		}
		else if (4 == pic_max_size) {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");

			if (still_size > SNAP_IMG_SIZE_4M) {
				still_size = SNAP_IMG_SIZE_4M;
			}
		} else if (2 == pic_max_size) {
			json_array.append("2M/1920*1080");

			if (still_size > SNAP_IMG_SIZE_2M) {
				still_size = SNAP_IMG_SIZE_2M;
			}
		} else {
			json_array.append("2M/1920*1080");
			json_array.append("4M/2560*1440");
			json_array.append("5M/2592*1944");
			json_array.append("8M/3840*2160");
			json_array.append("12M/4800*2700");
			json_array.append("18M/5760*3240");
			json_array.append("24M/6400*3600");
			json_array.append("32M/7680*4320");
			json_array.append("42M/8640*4864");

			if (still_size > SNAP_IMG_SIZE_42M) {
				still_size = SNAP_IMG_SIZE_42M;
			}
		}

		json_item_sub["options"] = json_array;
		json_item_sub["Selected"] = still_size + 1;
		json_item["CAM_ImageSize"] = json_item_sub;

		// Burst shooting
		json_item["CAM_Shooting"] = settings->burstNumber;
		// Shooting limitations
		json_item["CAM_MaxShooting"] = settings->shootingLimits;
	}
	
	// Video size
	json_array.clear();
	
	if (vid_max_size != 0) {
		uint8_t video_size = settings->videoSize;
		if (vid_max_size == 8 || vid_max_size == 4) {
			json_array.append("720P/30FPS");
			json_array.append("720P/60FPS");
	#ifdef VIDEO_SIZE_HD_120FPS
			json_array.append("720P/120FPS");
	#endif
	#ifdef VIDEO_SIZE_HD_240FPS
			json_array.append("720P/240FPS");
	#endif
			json_array.append("1080P/30FPS");
			json_array.append("1080P/60FPS");
	#ifdef VIDEO_SIZE_FHD_120FPS
			json_array.append("1080P/120FPS");
	#endif
			json_array.append("2K/30FPS");
			json_array.append("4K/30FPS");
			if (video_size > VIDEO_SIZE_4K2K_30FPS) {
				video_size = VIDEO_SIZE_4K2K_30FPS;
			}
		} else if (2 == vid_max_size) {
			json_array.append("720P/30FPS");
			json_array.append("720P/60FPS");
	#ifdef VIDEO_SIZE_HD_120FPS
			json_array.append("720P/120FPS");
	#endif
	#ifdef VIDEO_SIZE_HD_240FPS
			json_array.append("720P/240FPS");
	#endif
			json_array.append("1080P/30FPS");
			json_array.append("1080P/60FPS");
	#ifdef VIDEO_SIZE_FHD_120FPS
			json_array.append("1080P/120FPS");
	#endif
			json_array.append("2K/30FPS");

			if (video_size > VIDEO_SIZE_2K_30FPS) {
				video_size = VIDEO_SIZE_2K_30FPS;
			}
		} else if (1 == vid_max_size) {
			json_array.append("720P/30FPS");
			json_array.append("720P/60FPS");
	#ifdef VIDEO_SIZE_HD_120FPS
			json_array.append("720P/120FPS");
	#endif
	#ifdef VIDEO_SIZE_HD_240FPS
			json_array.append("720P/240FPS");
	#endif
			json_array.append("1080P/30FPS");
			json_array.append("1080P/60FPS");
	#ifdef VIDEO_SIZE_FHD_120FPS
			json_array.append("1080P/120FPS");
	#endif

			if (video_size > VIDEO_SIZE_FHD_60FPS) {
				video_size = VIDEO_SIZE_FHD_60FPS;
			}
		} else {
			json_array.append("720P/30FPS");
			json_array.append("720P/60FPS");
	#ifdef VIDEO_SIZE_HD_120FPS
			json_array.append("720P/120FPS");
	#endif
	#ifdef VIDEO_SIZE_HD_240FPS
			json_array.append("720P/240FPS");
	#endif
			json_array.append("1080P/30FPS");
			json_array.append("1080P/60FPS");
	#ifdef VIDEO_SIZE_FHD_120FPS
			json_array.append("1080P/120FPS");
	#endif
			json_array.append("2K/30FPS");
			json_array.append("4K/30FPS");

			if (video_size > VIDEO_SIZE_4K2K_30FPS) {
				video_size = VIDEO_SIZE_4K2K_30FPS;
			}
		}

		json_item_sub["options"] = json_array;
		json_item_sub["Selected"] = video_size + 1;
		json_item["CAM_VideoSize"] = json_item_sub;

		// Video length
		json_item["CAM_VideoLength"] = (settings->videoLength_h << 8) + settings->videoLength_l;
	}

	// PIR trigger settings
	json_item["PIR_Enable"] = settings->pirEn;
	json_item["PIR_Sensitivity"] = settings->ckPirSensitivity;
	json_item["PIR_Interval"] =
		settings->trigInterval_h * 3600 + settings->trigInterval_m * 60 + settings->trigInterval_s;

	// Timer settings
	json_item["Timer_Enable"] = settings->timerEn;
	char tmp_buffer[6];
	std::sprintf(tmp_buffer, "%02d:%02d", settings->timerLapse_h, settings->timerLapse_m);
	json_item["Timer_Interval"] = std::string(tmp_buffer);

	std::sprintf(tmp_buffer, "%02d:%02d", settings->timer1s_h, settings->timer1s_m);
	json_item["Timer_1Start"] = std::string(tmp_buffer);
	std::sprintf(tmp_buffer, "%02d:%02d", settings->timer1e_h, settings->timer1e_m);
	json_item["Timer_1End"] = std::string(tmp_buffer);
	std::sprintf(tmp_buffer, "%02d:%02d", settings->timer2s_h, settings->timer2s_m);
	json_item["Timer_2Start"] = std::string(tmp_buffer);
	std::sprintf(tmp_buffer, "%02d:%02d", settings->timer2e_h, settings->timer2e_m);
	json_item["Timer_2End"] = std::string(tmp_buffer);
	std::sprintf(tmp_buffer, "%02d:%02d", settings->timer3s_h, settings->timer3s_m);
	json_item["Timer_3Start"] = std::string(tmp_buffer);
	std::sprintf(tmp_buffer, "%02d:%02d", settings->timer3e_h, settings->timer3e_m);
	json_item["Timer_3End"] = std::string(tmp_buffer);

	val = settings->weekRepeats;
	
	for (int i = 0; i < 7; ++i) {
		buffer += (val & 0x40) ? '1' : '0';
		val <<= 1;
	}
	json_item["Timer_Repeats"] = buffer;

	// Misc settings
	json_item["Other_Stamp"] = settings->stampEn;
	json_item["Other_Cycle"] = settings->autoCover;

	// System settings
	auto upid = DeviceConfig::getInstance()->get(INI_SECTION_SYS, INI_KEY_UPID, "CKVISON");
	json_item["SYS_UPID"] = upid;

	auto pwd = DeviceConfig::getInstance()->get(INI_SECTION_SYS, INI_KEY_UPWD, "");
	json_item["SYS_Pwd"] = pwd;

	auto low_voltage = DeviceConfig::getInstance()->get(INI_SECTION_SYS, INI_KEY_LOW_VOL, "4.2");
	json_item["SYS_LowVoltage"] = low_voltage;

	auto end_voltage = DeviceConfig::getInstance()->get(INI_SECTION_SYS, INI_KEY_END_VOL, "4.0");
	json_item["SYS_EndVoltage"] = end_voltage;

	json_item["SYS_ONTime"] = settings->onTime_0 + (settings->onTime_1 << 8);
	json_item["SYS_HeartRate"] = settings->heartRate_0 + (settings->heartRate_1 << 8) +
				     (settings->heartRate_2 << 16) + (settings->heartRate_3 << 24);

	json_item["Dev_Name"] = settings->devName;

	// Network settings
	auto ms_ip =
		DeviceConfig::getInstance()->get(INI_SECTION_SERVER, INI_KEY_MS_IP, "www.aidetcloud.com");
	json_item["Server_MS"] = ms_ip;

	auto ms_port = DeviceConfig::getInstance()->get(INI_SECTION_SERVER, INI_KEY_MS_PORT, "8899");
	json_item["Server_MSPort"] = stoi_custom(ms_port);

	auto ntp_ip =
		DeviceConfig::getInstance()->get(INI_SECTION_SERVER, INI_KEY_NTP_IP, "www.aidetcloud.com");
	json_item["Server_NTP"] = ntp_ip;

	auto ntp_port = DeviceConfig::getInstance()->get(INI_SECTION_SERVER, INI_KEY_NTP_PORT, "123");
	json_item["Server_NTPPort"] = stoi_custom(ntp_port);

	auto fs_ip = DeviceConfig::getInstance()->get(INI_SECTION_SERVER, INI_KEY_FS_IP, "127.0.0.1");
	json_item["Server_FS"] = fs_ip;

	auto fs_port = DeviceConfig::getInstance()->get(INI_SECTION_SERVER, INI_KEY_FS_PORT, "80");
	json_item["Server_FSPort"] = stoi_custom(fs_port);

	json_item["Server_RTSP"] = "0";
	json_item["Server_RTSPPort"] = 0;

	auto device_config = DeviceConfig::getInstance();
	// File policy
	auto file_policy = device_config->get(INI_SECTION_POLICY, INI_KEY_FILE_MANAGE, 0);
	json_item["File_Policy"] = file_policy;

	// Record policy
	auto record_feature_on = device_config->get(INI_SECTION_POLICY, INI_KEY_RECORD, 0);
	if (record_feature_on == 1) {
		json_item["Record_Policy"] = settings->continuous_record;
	}
	auto remote_wakeup_feature_on = device_config->get(INI_SECTION_POLICY, INI_KEY_REMOTE_WAKEUP, 0);
	auto program_type = device_config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0);
	// Remote wake-up
	if (program_type == PTYPE_USB_DONGLE && remote_wakeup_feature_on == 1) {
		json_item["Remote_Wakeup"] = settings->remote_wakeup;
	}

	// 4G SIM number (phone number)
	if (program_type == PTYPE_USB_DONGLE) {
		std::string sim_num;
		Usb4gDongle::getInstance()->getSimNumber(sim_num);
		if (!sim_num.empty()) {
			json_item["Sim_Number"] = sim_num;
		}
	}

	// GPS
	json_item["GP"] = MCU::getInstance()->readGps();

	json_obj["param"] = json_item;
	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, json_obj);

	sendMessage(MSG_RC_GET_PARAM_ALL, message);
}

void RemoteCtrlClient::handleSetParamCommand(const Json::Value &root)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle set param command");
	auto settings = Settings::getInstance();
	auto device_config = DeviceConfig::getInstance();

	const Json::Value &param = root["param"];
	if (param.isNull()) {
		Logger::log(LogLevel::ERROR, "Invalid JSON: 'param' not found");
		return;
	}

	for (const auto &key : param.getMemberNames()) {
		const Json::Value &value = param[key];
		Logger::log(LogLevel::INFO, "Processing key: %s", key.c_str());

		if (key == "IS_MOBILE_APP") {
			remote_is_mobile_app = value.asBool();
			if (remote_is_mobile_app) {
				Logger::log(LogLevel::INFO, "From mobile app");
			}
		} else if (key == "CAM_Mode") {
			int cam_mode = value.asInt();
			Logger::log(LogLevel::INFO, "CAM_Mode -> %d", cam_mode);
			settings->realCameraMode = cam_mode;
			settings->cameraMode = cam_mode;
		} else if (key == "CAM_ImageSize") {
			int selected = value["Selected"].asInt();
			Logger::log(LogLevel::INFO, "CAM_ImageSize -> %d", selected);
			settings->stillSize = std::max(0, selected - 1);
		} else if (key == "CAM_Shooting") {
			int burst_number = value.asInt();
			Logger::log(LogLevel::INFO, "CAM_Shooting -> %d", burst_number);
			settings->burstNumber = burst_number;
		} else if (key == "CAM_MaxShooting") {
			int shooting_limits = value.asInt();
			Logger::log(LogLevel::INFO, "CAM_MaxShooting -> %d", shooting_limits);
			settings->shootingLimits = shooting_limits;
		} else if (key == "CAM_VideoSize") {
			int selected = value["Selected"].asInt();
			Logger::log(LogLevel::INFO, "CAM_VideoSize -> %d", selected);
			settings->videoSize = std::max(0, selected - 1);
		} else if (key == "CAM_VideoLength") {
			int video_length = value.asInt();
			Logger::log(LogLevel::INFO, "CAM_VideoLength -> %d", video_length);
			settings->videoLength_h = video_length >> 8;
			settings->videoLength_l = video_length & 0xFF;
		} else if (key == "PIR_Enable") {
			int pir_enable = value.asInt();
			Logger::log(LogLevel::INFO, "PIR_Enable -> %d", pir_enable);
			settings->pirEn = pir_enable;
		} else if (key == "PIR_Sensitivity") {
			int pir_sensitivity = value.asInt();
			Logger::log(LogLevel::INFO, "PIR_Sensitivity -> %d", pir_sensitivity);
			settings->ckPirSensitivity = pir_sensitivity;
		} else if (key == "PIR_Interval") {
			int interval = value.asInt();
			Logger::log(LogLevel::INFO, "PIR_Interval -> %d", interval);
			settings->trigInterval_s = interval % 60;
			settings->trigInterval_m = (interval / 60) % 60;
			settings->trigInterval_h = interval / 3600;
		} else if (key == "Timer_Enable") {
			int timer_enable = value.asInt();
			Logger::log(LogLevel::INFO, "Timer_Enable -> %d", timer_enable);
			settings->timerEn = timer_enable;
		} else if (key == "Timer_Interval") {
			std::string interval_str = value.asString();
			Logger::log(LogLevel::INFO, "Timer_Interval -> %s", interval_str.c_str());
			int interval = str2time(interval_str.c_str()) * 60;
			settings->timerLapse_s = interval % 60;
			settings->timerLapse_m = (interval / 60) % 60;
			settings->timerLapse_h = interval / 3600;
		} else if (key == "Timer_1Start") {
			std::string start_str = value.asString();
			Logger::log(LogLevel::INFO, "Timer_1Start -> %s", start_str.c_str());
			int start_time = str2time(start_str.c_str());
			settings->timer1s_h = start_time / 60;
			settings->timer1s_m = start_time % 60;
		} else if (key == "Timer_1End") {
			std::string end_str = value.asString();
			Logger::log(LogLevel::INFO, "Timer_1End -> %s", end_str.c_str());
			int end_time = str2time(end_str.c_str());
			settings->timer1e_h = end_time / 60;
			settings->timer1e_m = end_time % 60;
		} else if (key == "Timer_2Start") {
			std::string start_str = value.asString();
			Logger::log(LogLevel::INFO, "Timer_2Start -> %s", start_str.c_str());
			int start_time = str2time(start_str.c_str());
			settings->timer2s_h = start_time / 60;
			settings->timer2s_m = start_time % 60;
		} else if (key == "Timer_2End") {
			std::string end_str = value.asString();
			Logger::log(LogLevel::INFO, "Timer_2End -> %s", end_str.c_str());
			int end_time = str2time(end_str.c_str());
			settings->timer2e_h = end_time / 60;
			settings->timer2e_m = end_time % 60;
		} else if (key == "Timer_3Start") {
			std::string start_str = value.asString();
			Logger::log(LogLevel::INFO, "Timer_3Start -> %s", start_str.c_str());
			int start_time = str2time(start_str.c_str());
			settings->timer3s_h = start_time / 60;
			settings->timer3s_m = start_time % 60;
		} else if (key == "Timer_3End") {
			std::string end_str = value.asString();
			Logger::log(LogLevel::INFO, "Timer_3End -> %s", end_str.c_str());
			int end_time = str2time(end_str.c_str());
			settings->timer3e_h = end_time / 60;
			settings->timer3e_m = end_time % 60;
		} else if (key == "Timer_Repeats") {
			std::string repeats_str = value.asString();
			int repeats = str2week(repeats_str.c_str());
			Logger::log(LogLevel::INFO, "Timer_Repeats -> %s %d", repeats_str.c_str(), repeats);
			settings->weekRepeats = repeats;
		} else if (key == "Other_Cycle") {
			int auto_cover = value.asInt();
			Logger::log(LogLevel::INFO, "Other_Cycle -> %d", auto_cover);
			settings->autoCover = auto_cover;
		} else if (key == "Other_Stamp") {
			int stamp_en = value.asInt();
			Logger::log(LogLevel::INFO, "Other_Stamp -> %d", stamp_en);
			settings->stampEn = stamp_en;
		} else if (key == "SYS_ONTime") {
			int on_time = value.asInt();
			Logger::log(LogLevel::INFO, "SYS_ONTime -> %d", on_time);
			settings->onTime_0 = on_time & 0xFF;
			settings->onTime_1 = (on_time >> 8) & 0xFF;
		} else if (key == "SYS_HeartRate") {
			int heart_rate = value.asInt();
			Logger::log(LogLevel::INFO, "SYS_HeartRate -> %d", heart_rate);
			settings->heartRate_0 = heart_rate & 0xFF;
			settings->heartRate_1 = (heart_rate >> 8) & 0xFF;
			settings->heartRate_2 = (heart_rate >> 16) & 0xFF;
			settings->heartRate_3 = (heart_rate >> 24) & 0xFF;
		} else if (key == "SYS_UPID") {
			std::string upid = value.asString();
			Logger::log(LogLevel::INFO, "SYS_UPID -> %s", upid.c_str());
			device_config->set(INI_SECTION_SYS, INI_KEY_UPID, upid);
		} else if (key == "SYS_Pwd") {
			std::string pwd = value.asString();
			Logger::log(LogLevel::INFO, "SYS_Pwd -> %s", pwd.c_str());
			device_config->set(INI_SECTION_SYS, INI_KEY_UPWD, pwd);
		} else if (key == "Dev_NameEn") {
			int show_dev_name_en = value.asInt();
			Logger::log(LogLevel::INFO, "Dev_NameEn -> %d", show_dev_name_en);
			settings->showDevNameEn = show_dev_name_en;
		} else if (key == "Dev_Name") {
			std::string dev_name = value.asString();
			Logger::log(LogLevel::INFO, "Dev_Name -> %s", dev_name.c_str());
			std::fill(std::begin(settings->devName), std::end(settings->devName), 0);
			std::copy_n(dev_name.c_str(), std::min(dev_name.size(), sizeof(settings->devName)),
				    settings->devName);
		} else if (key == "Dev_Pwd") {
			std::string dev_pwd = value.asString();
			Logger::log(LogLevel::INFO, "Dev_Pwd -> %s", dev_pwd.c_str());
			std::fill(std::begin(settings->devPwd), std::end(settings->devPwd), 0);
			std::copy_n(dev_pwd.c_str(), std::min(dev_pwd.size(), sizeof(settings->devPwd)),
				    settings->devPwd);
		} else if (key == "Dev_PwdEn") {
			int pwd_en = value.asInt();
			Logger::log(LogLevel::INFO, "Dev_PwdEn -> %d", pwd_en);
			settings->pwdEn = pwd_en;
		} else if (key == "GP") {
			std::string gps_str = value.asString();
			Logger::log(LogLevel::INFO, "GP -> %s", gps_str.c_str());
			MCU::getInstance()->writeGps(gps_str);
		} else if (key == "Server_MS") {
			std::string server_ms = value.asString();
			Logger::log(LogLevel::INFO, "Server_MS -> %s", server_ms.c_str());
			device_config->set(INI_SECTION_SERVER, INI_KEY_MS_IP, server_ms);
		} else if (key == "Server_MSPort") {
			int ms_port = value.asInt();
			Logger::log(LogLevel::INFO, "Server_MSPort -> %d", ms_port);
			device_config->set(INI_SECTION_SERVER, INI_KEY_MS_PORT, to_string_custom(ms_port));
		} else if (key == "Server_NTP") {
			std::string server_ntp = value.asString();
			Logger::log(LogLevel::INFO, "Server_NTP -> %s", server_ntp.c_str());
			device_config->set(INI_SECTION_SERVER, INI_KEY_NTP_IP, server_ntp);
		} else if (key == "Server_NTPPort") {
			int ntp_port = value.asInt();
			Logger::log(LogLevel::INFO, "Server_NTPPort -> %d", ntp_port);
			device_config->set(INI_SECTION_SERVER, INI_KEY_NTP_PORT, to_string_custom(ntp_port));
		} else if (key == "File_Policy") {
			int file_policy = value.asInt();
			Logger::log(LogLevel::INFO, "File_Policy -> %d", file_policy);
			device_config->set(INI_SECTION_POLICY, INI_KEY_FILE_MANAGE, file_policy);
		} else if (key == "Record_Policy") {
			int record_policy = value.asInt();
			Logger::log(LogLevel::INFO, "Record_Policy -> %d", record_policy);
			settings->continuous_record = static_cast<uint32_t>(record_policy);
		} else if (key == "Remote_Wakeup") {
			int remote_wakeup = value.asInt();
			Logger::log(LogLevel::INFO, "Remote_Wakeup -> %d", remote_wakeup);
			settings->remote_wakeup = static_cast<uint32_t>(remote_wakeup);
			MCU::getInstance()->writeRemoteWakeup(settings->remote_wakeup);
		}
	}

	Json::Value response;
	response["status"] = 0;
	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, response);

	sendMessage(MSG_RC_SET_PARAM, message);
}

void RemoteCtrlClient::handleResetParamCommand(const Json::Value &root)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle reset param command");

	auto settings = Settings::getInstance();
	uint8_t is_wled = settings->isWLed;
	uint8_t br4k = settings->bitRate_4k;
	uint8_t br1080p = settings->bitRate_1080p;
	uint8_t br720p = settings->bitRate_720p;

	settings->isWLed = is_wled;
	settings->bitRate_4k = br4k;
	settings->bitRate_1080p = br1080p;
	settings->bitRate_720p = br720p;

	//ck_download_param_to_mcu();//TODO

	Json::Value response;
	response["status"] = 0;

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, response);

	sendMessage(MSG_RC_RESET_PARAM, message);
}

void RemoteCtrlClient::handleSetDatetimeCommand(const Json::Value &root)
{
    Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle set datetime command");
    int status = 0;
    const Json::Value &param = root["param"];
    if (param.isNull()) {
        Logger::log(LogLevel::ERROR, "Invalid JSON: 'param' not found");
        status = -1;
    } else {
        std::string datetime_str = param.asString();
        Logger::log(LogLevel::INFO, "time: %s", datetime_str.c_str());

        int year, month, day, hour, min, sec;
        if (sscanf(datetime_str.c_str(), "%d-%d-%dT%d:%d:%d.000", &year, &month, &day, &hour, &min, &sec) != 6) {
            Logger::log(LogLevel::ERROR, "Failed to parse datetime");
            status = -1;
        } else {
            Logger::log(LogLevel::INFO, "%d %d %d %d:%d:%d", year, month, day, hour, min, sec);

            // Set system time
            struct tm new_time;
            memset(&new_time, 0, sizeof(new_time));
            new_time.tm_year = year - YEAR_OFFSET;
            new_time.tm_mon = month - MONTH_OFFSET;
            new_time.tm_mday = day;
            new_time.tm_hour = hour;
            new_time.tm_min = min;
            new_time.tm_sec = sec;

            time_t time_in_sec = mktime(&new_time);
            if (time_in_sec == -1) {
                Logger::log(LogLevel::ERROR, "Failed to convert time");
                status = -1;
            } else {
                struct timeval tv;
                tv.tv_sec = time_in_sec;
                tv.tv_usec = 0;
                if (settimeofday(&tv, nullptr) != 0) {
                    Logger::log(LogLevel::ERROR, "Failed to set system time");
                    status = -1;
                } else {
                    // Set RTC time
                    if (!RTC::getInstance()->setTime(new_time)) {
                        Logger::log(LogLevel::ERROR, "Failed to set RTC time");
                        status = -1;
                    } else {
                        // Set MCU time
                        auto useGpsTime = MCU::getInstance()->useGpsTime();
                        if (useGpsTime) {
                            Logger::log(LogLevel::INFO, "Use GPS time");
                        } else {
                            Logger::log(LogLevel::INFO, "Use RTC time");
                            if (!MCU::getInstance()->setDatetime(&new_time)) {
                                Logger::log(LogLevel::ERROR, "Failed to set MCU time");
                                status = -1;
                            }
                        }
                    }
                }
            }
        }
    }

    Json::Value response;
    response["status"] = status;
    Json::StreamWriterBuilder writer;
    std::string message = Json::writeString(writer, response);
    Logger::log(LogLevel::INFO, "dump json: %s", message.c_str());

    sendMessage(MSG_RC_SET_DATETIME, message);
}

void RemoteCtrlClient::handleEnterWorkingModeCommand(const Json::Value &root)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle enter working mode command");

	Json::Value json_obj;
	json_obj["status"] = 0;

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, json_obj);
	
	sendMessage(MSG_RC_ENTER_WORK_MODE, message);

	Power::getInstance()->requestChangeMode();
}
void RemoteCtrlClient::handleGetHwInfoCommand(const Json::Value &root)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle get hardware info command");

	//TODO: format sdcard
	Json::Value json_obj;
	json_obj["status"] = 0;
	json_obj["pid"] = "xxx";
	json_obj["camera_ver"] = "xxx";
	json_obj["camera_model"] = "xxx";
	json_obj["camera_build"] = "xxx";
	json_obj["mcu_ver"] = "xxx";

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, json_obj);
	
	sendMessage(MSG_RC_GET_HW_INFO, message);
}

void RemoteCtrlClient::handleFormatSdcardCommand(const Json::Value &root)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle format sdcard command");

	//TODO: format sdcard
	Json::Value json_obj;
	json_obj["status"] = 0;

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, json_obj);
	

	sendMessage(MSG_RC_FORMAT_SDCARD, message);
}

void RemoteCtrlClient::handleGetSensorInfoCommand(const Json::Value &root)
{
	Logger::log(LogLevel::INFO, "RemoteCtrlClient: Handle get sensor info command");
	Json::Value resp_root;
	char tmp_buffer[64] = { 0 };

	auto mcu = MCU::getInstance();
	auto device_config = DeviceConfig::getInstance();
	//get battery info
	auto battery_voltage = mcu->readBatteryVoltage();
	auto battery_level = mcu->readBatteryLevel();
	auto external_voltage = mcu->readExternalVoltage();
	auto battery_type = mcu->readBatteryType();
	resp_root["battery"] = battery_voltage;
	resp_root["battery_type"] = battery_type;
	resp_root["battery_level"] = (battery_voltage >= 6 && battery_voltage < 14) ? 0 : battery_level;
	resp_root["ext_power"] = external_voltage;

	//get sdcard info
	auto disk_info = Disk::getInfo(DISK_PATHNAME);
	resp_root["sdcard_capacity"] = disk_info.total;
	resp_root["sdcard_used"] = disk_info.total - disk_info.free;

	//get other sensor info
	resp_root["cds"] = mcu->readCds();
	resp_root["temp"] = to_string_custom(mcu->readTemperature());
	resp_root["press"] = to_string_custom(mcu->readAtmosPressure());
	resp_root["rh"] = to_string_custom(mcu->readHumidity());

	//get device info
	auto pid = device_config->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	resp_root["pid"] = pid;
	resp_root["camera_ver"] = CAMERA_VERSION;
	resp_root["camera_model"] = device_config->get(INI_SECTION_BOOT, INI_KEY_PMODEL, "");
	resp_root["camera_build"] = CAMERA_BUILD_TIME;
	resp_root["mcu_ver"] = mcu->readVersion();
/*
//TODO:
//get network info
#if USB_NET_SUPPORT
	if (ck_get_program_type() == 4) {
		resp_root["wifi_4g"] = 1;
		resp_root["rssi_4g"] = ck_get_4g_signal();
		resp_root["act_4g"] = ck_get_4g_act();
	} else {
		resp_root["wifi_4g"] = ck_is_4g_wifi_rdy();
		resp_root["rssi_4g"] = ck_4g_rssi();
		resp_root["act_4g"] = ck_4g_act();
	}
#else
	resp_root["wifi_4g"] = ck_is_4g_wifi_rdy();
	resp_root["rssi_4g"] = ck_4g_rssi();
	resp_root["act_4g"] = ck_4g_act();
#endif
*/
	//get datetime info
	struct tm* datetime = nullptr;
	time_t now = time(nullptr);
    datetime = localtime(&now);
	strftime(tmp_buffer, sizeof(tmp_buffer), "%Y-%m-%dT%H:%M:%S.000", datetime);
	resp_root["datetime"] = tmp_buffer;

	resp_root["status"] = 0;

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, resp_root);

	sendMessage(MSG_RC_GET_SENSOR_INFO, message);
}

int RemoteCtrlClient::str2time(const std::string& str)
{
	return str2time(str.c_str());
}

int RemoteCtrlClient::str2time(const char * str)
{
	int i = 0, j = 0;
	if ( str )
		sscanf( str, "%d:%d", &i, &j );
	else{
		i = 0, j = 0;
	}

	return i * 60 + j;
}

int RemoteCtrlClient::str2week(const std::string& str)
{
	return str2week(str.c_str());
}

int RemoteCtrlClient::str2week(char * str)
{
	int i = 0;
	int w = 0;

	for ( i = 0; i < 7; i++ )
	{
		w <<= 1;
		if ( str[i] == '1' )
			w |= 1;
	}

	return w;
}