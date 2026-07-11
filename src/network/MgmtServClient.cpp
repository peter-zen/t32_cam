#include <iostream>
#include <sstream>
#include <iomanip>
#include <json/json.h>
#include <sys/time.h>
#include <time.h>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <unistd.h>
#include <arpa/inet.h>
#include <condition_variable>
#include <mutex>
#include <md5.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/statvfs.h>
#include "Common.h"
#include "MgmtServClient.h"
#include "Broadcast.h"
#include "FirmwareUpdate.h"
#include "utils/base64/Base64.h"
#include "MCU.h"
#include "Rtmp.h"
#include "Settings.h"
#include "DeviceConfig.h"
#include "ProductConfig.h"
#include "EnvManager.h"
#include "Logger.h"
#include "UsbDongle.h"
#include "Disk.h"
#include "StringConvert.h"
#include "Misc.h"
#include "Timezone.h"

using namespace network;

namespace {

int authTimeoutMs()
{
	const char *env = std::getenv("HTC_AUTH_TIMEOUT_MS");
	if (env && env[0] != '\0') {
		int v = std::atoi(env);
		if (v > 0) {
			return v;
		}
	}
	return 10000;
}

bool isSyncKeyPidShapeSupported(const std::string &pid)
{
	if (pid.length() < 5) {
		return false;
	}
	for (unsigned char c : pid) {
		if (!std::isalnum(c)) {
			return false;
		}
	}

	const char c = pid[pid.length() - 1];
	return (c >= '0' && c <= '9') ||
	       (std::strchr("QWERTYUIOP", c) != nullptr) ||
	       (std::strchr("ASDFGHJKL", c) != nullptr) ||
	       (std::strchr("ZXCVBNM", c) != nullptr);
}

}  // namespace

MgmtServClient::MgmtServClient(const std::string &address, int port)
	: Client(address, port)
{
	enable_broadcast = 0;
	enable_firmware_update = 0;
	need_euid = true;
	remote_wakeup = false;
	auth_success.store(false);
	auth_result_received.store(false);
	storage_serv_client = nullptr;
	Logger::log(LogLevel::INFO, "MgmtServClient: Constructor");
}

MgmtServClient::~MgmtServClient()
{
	Logger::log(LogLevel::INFO, "MgmtServClient: Destructor");
	stop();
}

std::shared_ptr<StorageServClient> MgmtServClient::newStorageServClient()
{
	storage_serv_client = std::make_shared<StorageServClient>(socket_fd);
	return storage_serv_client;
}

int MgmtServClient::getCode(const char c)
{
	if (c <= '9')
		return c - '0';

	return c - 'A' + 1;
}

std::string MgmtServClient::generateSyncKey(const std::string &pid, int dev_type)
{
	char sync_key[48] = { 0 };
	int code_len = 0;
	int code_pos = 0;
	int pid_len = 0;
	int d = 0;
	char c = 0;
	int i = 0;
	int k = 0;

	const char *alphabet1 = "QWERTYUIOP";
	const char *alphabet2 = "ASDFGHJKL";
	const char *alphabet3 = "ZXCVBNM";
	const char *alpha = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

	if (pid.empty()) {
		return "";
	}

	dev_type = pid[0] % 4;

	pid_len = pid.length();
	c = pid[pid_len - 1];

	if (c >= '0' && c <= '9') {
		d = pid[pid_len - 1] - '0';
		code_len = (d % 2 == 0) ? 25 : 20;
	} else if (strchr(alphabet1, c) != nullptr) {
		code_len = 26;
	} else if (strchr(alphabet2, c) != nullptr) {
		code_len = 24;
	} else if (strchr(alphabet3, c) != nullptr) {
		code_len = 18;
	} else {
		return "";
	}

	memset(sync_key, 0, sizeof(sync_key));

	code_pos = dev_type + (getCode(pid[2]) % 4) + (getCode(pid[4]) % 4);

	d = 0;
	for (i = 0; i < 4; i++) {
		k = getCode(pid[pid_len - 4 + i]);
		d = (k < 10) ? (d * 10 + k) : (d * 100 + k);
	}

	srand((d * code_pos) * 100 + code_len);
	for (i = 0; i < code_pos; i++) {
		sync_key[i] = alpha[rand() % 62];
	}

	sprintf(&sync_key[code_pos], "%d%d", (d * code_pos), code_len);

	k = strlen(sync_key);
	for (i = code_pos; i < code_len; i++) {
		sync_key[k + i - code_pos] = alpha[rand() % 62];
	}

	return std::string(sync_key);
}

int MgmtServClient::authenticate()
{
	Logger::log(LogLevel::INFO, "MgmtServClient: Authenticate device");
	auto security_code = ProductConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_SMODE, 0);
	Logger::log(LogLevel::INFO, "MgmtServClient: Security code: %d", security_code);
	std::string pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	std::string config_file = EnvManager::getInstance()->getEnv("CONFIG_FILE", "");
	int timeout_ms = authTimeoutMs();
	Logger::log(LogLevel::INFO, "MgmtServClient: CONFIG_FILE: %s", config_file.c_str());
	Logger::log(LogLevel::INFO, "MgmtServClient: PID: %s", pid.c_str());
	Logger::log(LogLevel::INFO, "MgmtServClient: auth timeout: %d ms", timeout_ms);
	if (security_code == 0) {
		Logger::log(LogLevel::INFO, "MgmtServClient: generating sync key");
		if (!isSyncKeyPidShapeSupported(pid)) {
			Logger::log(LogLevel::ERROR,
			            "MgmtServClient: PID cannot generate a server-compatible Sync_Key: %s",
			            pid.c_str());
			return EC_FAILED;
		}
		sync_key = generateSyncKey(pid, dev_type::DEV_CAMERA);
	} else {
		Logger::log(LogLevel::INFO, "MgmtServClient: encoding sync key");
		char buffer[48] = { 0 };
		uint32_t output_data_len = 0;
		if (Base64::encode((char*)pid.c_str(), pid.length(), buffer, &output_data_len)) {
			sync_key = std::string(buffer);
		}
	}

	if (sync_key.empty()) {
		Logger::log(LogLevel::ERROR, "MgmtServClient: Sync key is empty");
		return EC_FAILED;
	}

	Json::Value root;
	root["PID"] = pid;
	root["Sync_Key"] = sync_key;
	root["EUID"] = need_euid ? "1" : "0";
	root["FW_Version"] = MCU::getInstance()->readFirmwareVersion();
	root["PName"] = ProductConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PNAME, "");
	root["API_Version"] = "V1";
	if (remote_wakeup) {
		root["Remote_Wakeup"] = "1";
	}
	Logger::log(LogLevel::INFO, "MgmtServClient: auth message ready");

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, root);

	auth_result_received.store(false);
	auth_success.store(false);

	Logger::log(LogLevel::INFO, "MgmtServClient: sending auth message");
	if (EC_SUCCESS != sendMessage(MSG_TYPE_AUTH, message)) {
		Logger::log(LogLevel::ERROR, "MgmtServClient: send auth message failed");
		return EC_FAILED;
	}
	Logger::log(LogLevel::INFO, "MgmtServClient: auth message sent, waiting response");

	//wait for auth response
	{
		std::unique_lock<std::mutex> lock(auth_mutex);
		bool received = auth_cv.wait_for(
			lock
			, std::chrono::milliseconds(timeout_ms)
			, [this] { return auth_result_received.load();}
		);
		if (!received) {
			Logger::log(LogLevel::ERROR, "MgmtServClient: auth response timeout after %d ms", timeout_ms);
		}
	}

	return auth_success.load() ? EC_SUCCESS : EC_FAILED;
}

int MgmtServClient::receiveCommand(char *buffer, size_t length)
{
	// receive header
	size_t headerLen = 12;
	memset(buffer, 0, length);
	int ret = receive(buffer, headerLen, 0);

	if ((int)headerLen == ret) {
		int message_length = 0;
		memcpy(&message_length, &buffer[8], 4);

		// receive json
		ret = receive(buffer + 12, message_length, 0);

		if (message_length == ret) {
			return message_length;
		}
	}

	return 0;
}

void MgmtServClient::processCommand(std::unique_ptr<char[]> &buffer, size_t length)
{
	int total_length = 0;
	int type = 0;
	int msg_length = 0;

	memcpy(&total_length, buffer.get(), 4);
	memcpy(&type, buffer.get() + 4, 4);
	memcpy(&msg_length, buffer.get() + 8, 4);
	const char *payload = buffer.get() + 12;

	Logger::log(LogLevel::DEBUG, "Total: %d, Type: 0x%x, MsgLength: %d", total_length, type, msg_length);

	Json::CharReaderBuilder reader;
	Json::Value root;
	std::string errs;
	std::istringstream s(payload);
	std::string str_json = s.str();
	Logger::log(LogLevel::INFO, "Received message: %s", str_json.c_str());
	if (!Json::parseFromStream(reader, s, &root, &errs)) {
		Logger::log(LogLevel::ERROR, "Failed to parse JSON: %s", errs.c_str());
		return;
	}

	switch (type) {
	case MSG_TYPE_AUTH:
		handleAuthCommand(root);
		break;
	case MSG_TYPE_UPLOAD_FILE:
		if (storage_serv_client) {
			storage_serv_client->processCommand(type, root);
		}
		break;
	case MSG_TYPE_FILE_LIST:
		handleFileListCommand(root);
		break;
	case MSG_TYPE_RTMP:
		handleRtmpCommand(root);
		break;
	case MSG_TYPE_SETTING:
		handleSettingCommand(root);
		break;
	case MSG_TYPE_DOWNLOAD_FILE:
		handleDownloadFileCommand(root);
		break;
	case MSG_TYPE_VOICE_BROADCAST_MARK:
	case MSG_TYPE_FIRMWARE_MARK:
		break;
	default:
		Logger::log(LogLevel::WARNING, "Unknown message type: %d", type);
		break;
	}
}

void MgmtServClient::handleAuthCommand(const Json::Value &root)
{
	auto settings = Settings::getInstance();
	settings->comm_code = root.get("Comm_Code", "").asString();
	settings->euid = root.get("EUID", "").asString();
	settings->duid = root.get("DUID", "").asString();
	settings->setting_mark = root.get("Setting_Mark", 0).asInt();
	settings->enable_firmware_update = root.get("Firmware_Update", 0).asInt();
	int status_id = root.get("Status_ID", 0).asInt();
	std::string error_description;
	if (root.isMember("Error_Description") && root["Error_Description"].isString()) {
		error_description = root["Error_Description"].asString();
	}

	//notify auth result
	auth_success.store(status_id == 0);
	auth_result_received.store(true);
	auth_cv.notify_one();

	Logger::log(LogLevel::DEBUG, "Comm_Code: %s", settings->comm_code.c_str());
	Logger::log(LogLevel::DEBUG, "EUID: %s", settings->euid.c_str());
	Logger::log(LogLevel::DEBUG, "DUID: %s", settings->duid.c_str());
	Logger::log(LogLevel::DEBUG, "Setting_Mark: %d", settings->setting_mark);
	Logger::log(LogLevel::DEBUG, "Firmware_Update: %d", settings->enable_firmware_update);
	Logger::log(LogLevel::DEBUG, "Status_ID: %d", status_id);
	if (status_id == 0) {
		Logger::log(LogLevel::INFO, "MgmtServClient: auth accepted");
	} else {
		Logger::log(LogLevel::ERROR, "MgmtServClient: auth rejected status=%d error=%s",
		            status_id, error_description.c_str());
	}
	
	if (status_id == 0) {
		enable_broadcast = root.get("Voice_Broadcast", 0).asInt();
		Logger::log(LogLevel::DEBUG, "Voice_Broadcast: %d", enable_broadcast);
		if (enable_broadcast == 1) {
			Broadcast::getInstance()->bindSendMessageCallback([this](int message_type, const std::string &message) {
				return this->sendMessage(message_type, message);
			});
			Broadcast::getInstance()->requestFileList();
		} else {
			enable_firmware_update = root.get("Firmware_Update", 0).asInt();
			Logger::log(LogLevel::DEBUG, "Firmware_Update: %d", enable_firmware_update);
			if (enable_firmware_update == 1) {
				FirmwareUpdate::getInstance()->bindSendMessageCallback([this](int message_type, const std::string &message) {
					return this->sendMessage(message_type, message);
				});
				FirmwareUpdate::getInstance()->requestFileList();
			}
		}
	}
}

void MgmtServClient::handleFileListCommand(const Json::Value &root)
{
	int status_id = root.get("Status_ID", 0).asInt();
	Logger::log(LogLevel::DEBUG, "Status_ID: %d", status_id);

	if (status_id == 0) {
		if (enable_broadcast == 1) {
			Logger::log(LogLevel::DEBUG, "Broadcast: %d", enable_broadcast);
			Broadcast::getInstance()->processFileList(root);
		} else if (enable_firmware_update == 1) {
			Logger::log(LogLevel::DEBUG, "Firmware_Update: %d", enable_firmware_update);
			FirmwareUpdate::getInstance()->processFileList(root);
		}
	}
}

void MgmtServClient::handleRtmpCommand(const Json::Value &root)
{
	int status_id = root.get("Status_ID", 0).asInt();
	Logger::log(LogLevel::DEBUG, "Status_ID: %d", status_id);

	if (status_id == 0) {
		const Json::Value msgType = root["Msg_Type"];
		if (!msgType.isNull() && msgType.asInt() == MSG_TYPE_RTMP) {
			const Json::Value url = root["URL"];
			if (!url.isNull()) {
				std::string rtmpURL = url.asString();
				strcpy(rtmp_url, rtmpURL.c_str());
				const Json::Value expire = root["Expire"];
				if (!expire.isNull()) {
					rtmp_duration = expire.asInt();
					if (rtmp_ongoing) {
						MCU::getInstance()->waitFor(rtmp_duration + 5);
					}
					rtmp = std::make_shared<Rtmp>(rtmpURL, rtmp_duration, [this](int error_code) {
						if (error_code == EC_SUCCESS) {
							rtmp_ongoing = false;
						}
					});

					if (rtmp && rtmp->start()) {
						rtmp_ongoing = true;
					} else {
						rtmp_ongoing = false;
					}
				}
			}
		}
	}
}

void MgmtServClient::handleDownloadFileCommand(const Json::Value &root)
{
	int status_id = root.get("Status_ID", 0).asInt();
	Logger::log(LogLevel::DEBUG, "Status_ID: %d", status_id);
	const Json::Value &file_name_json = root["FileName"];
	const Json::Value &file_type_json = root["FileType"];
	const Json::Value &file_size_json = root["FileSize"];
	const Json::Value &file_check_code_json = root["File_Check_Code"];
	if (status_id == 0) {
		std::string file_name = file_name_json.isString() ? file_name_json.asString() : "";
		std::string file_type = file_type_json.isString() ? file_type_json.asString() : "";
		int file_size = file_size_json.isInt() ? file_size_json.asInt() : 0;
		std::string check_code = file_check_code_json.isString() ? file_check_code_json.asString() : "";

		// recv file data
		size_t recved_data_size = 0;
		auto file_buffer = std::shared_ptr<char>(new char[file_size], [](char* p) {
			delete[] p;
		});

		if (file_buffer) {
			memset(file_buffer.get(), 0, file_size);
			while ((int)recved_data_size < file_size) {
				// select
				fd_set read_set;
				struct timeval timeout;
				FD_ZERO(&read_set);
				FD_SET(socket_fd, &read_set);
				timeout.tv_sec = 1;
				timeout.tv_usec = 0;
				if (select(socket_fd + 1, &read_set, nullptr, nullptr, &timeout) <= 0) {
					continue;
				}

				if (!FD_ISSET(socket_fd, &read_set)) {
					continue;
				}

				// recv
				char temp_buf[32] = { 0 };
				ssize_t ret = recv(socket_fd, temp_buf, sizeof(temp_buf), MSG_PEEK | MSG_DONTWAIT);
				if (ret > 0) {
					ret = recv(socket_fd, file_buffer.get() + recved_data_size,
						   file_size - recved_data_size, 0);
					if (ret > 0) {
						recved_data_size += ret;
					}
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(20));
			}

			unsigned char digest[16] = { 0 };
			MD5_CTX ctx;
			MD5Init(&ctx);
			MD5Update(&ctx, reinterpret_cast<unsigned char*>(file_buffer.get()), static_cast<unsigned int>(file_size));
			MD5Final(digest, &ctx);

			char temp_buff[32] = { 0 };
			memcpy(temp_buff, check_code.c_str(), 32);
			HTTPStrHToAscii(temp_buff);
			if (memcmp(digest, temp_buff, 16) == 0) {
				if (enable_broadcast == 1) {
					Broadcast::getInstance()->processFile(file_name, file_buffer, file_size);
				} else if (enable_firmware_update == 1) {
					FirmwareUpdate::getInstance()->processFile("", file_buffer, file_size);
				}
			}
		}
	}
}

void MgmtServClient::handleSettingCommand(const Json::Value &root)
{
	int status_id = root.get("Status_ID", 0).asInt();
	Logger::log(LogLevel::DEBUG, "Status_ID: %d", status_id);
	auto settings = Settings::getInstance();
	for (Json::ValueConstIterator it = root.begin(); it != root.end(); ++it) {
		const std::string key = it.key().asString();
		const Json::Value &value = *it;
		Logger::log(LogLevel::INFO, "%s : %s", key.c_str(), value.toStyledString().c_str());

		// Capture settings
		if (key == "CAM_Mode") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->realCameraMode = v;
			settings->cameraMode = v;
		} else if (key == "CAM_ImageSize") {
			int v = value["Selected"].asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->stillSize = v - 1 >= 0 ? v - 1 : 0;
		} else if (key == "CAM_Shooting") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->burstNumber = v;
		} else if (key == "CAM_MaxShooting") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->shootingLimits = v;
		} else if (key == "CAM_VideoSize") {
			int v = value["Selected"].asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->videoSize = v - 1 >= 0 ? v - 1 : 0;
		} else if (key == "CAM_VideoLength") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->videoLength_h = (v >> 8) & 0xFF;
			settings->videoLength_l = v & 0xFF;
		}
		// PIR trigger settings
		else if (key == "PIR_Enable") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->pirEn = v;
		} else if (key == "PIR_Sensitivity") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->ckPirSensitivity = v;
		} else if (key == "PIR_Interval") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr);
			settings->trigInterval_s = v % 60;
			settings->trigInterval_m = ((v - settings->trigInterval_s) / 60) % 60;
			settings->trigInterval_h =
				((v - settings->trigInterval_s - 60 * settings->trigInterval_m) / 3600) % 60;
		}
		// Timer settings
		else if (key == "Timer_Enable") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->timerEn = v;
		} else if (key == "Timer_Interval") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr) * 60;
			settings->timerLapse_s = v % 60;
			settings->timerLapse_m = ((v - settings->timerLapse_s) / 60) % 60;
			settings->timerLapse_h =
				((v - settings->timerLapse_s - 60 * settings->timerLapse_m) / 3600) % 60;
		} else if (key == "Timer_1Start") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr);
			settings->timer1s_h = v / 60;
			settings->timer1s_m = v % 60;
		} else if (key == "Timer_1End") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr);
			settings->timer1e_h = v / 60;
			settings->timer1e_m = v % 60;
		} else if (key == "Timer_2Start") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr);
			settings->timer2s_h = v / 60;
			settings->timer2s_m = v % 60;
		} else if (key == "Timer_2End") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr);
			settings->timer2e_h = v / 60;
			settings->timer2e_m = v % 60;
		} else if (key == "Timer_3Start") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr);
			settings->timer3s_h = v / 60;
			settings->timer3s_m = v % 60;
		} else if (key == "Timer_3End") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			int v = str2time(ptr);
			settings->timer3e_h = v / 60;
			settings->timer3e_m = v % 60;
		} else if (key == "Timer_Repeats") {
			std::string ptr = value.asString();
			int v = str2week(ptr);
			Logger::log(LogLevel::INFO, " -> %s %d", ptr.c_str(), v);
			settings->weekRepeats = v;
		}
		// Misc settings
		else if (key == "Other_Cycle") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->autoCover = v;
		} else if (key == "Other_Stamp") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->stampEn = v;
		} else if (key == "Other_DevName") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
			if (!ptr.empty()) {
				strcpy((char*)settings->devName, ptr.c_str());
			} else {
				memset((char*)settings->devName, 0, sizeof(settings->devName));
			}
		}
		// System settings
		else if (key == "SYS_UPID") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "SYS_LowVoltage") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "SYS_EndVoltage") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "SYS_ONTime") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->onTime_0 = v & 0xFF;
			settings->onTime_1 = (v >> 8) & 0xFF;
		} else if (key == "SYS_HeartRate") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
			settings->heartRate_0 = v & 0xFF;
			settings->heartRate_1 = (v >> 8) & 0xFF;
			settings->heartRate_2 = (v >> 16) & 0xFF;
			settings->heartRate_3 = (v >> 24) & 0xFF;
		}
		// Network settings
		else if (key == "Server_NTP") {
			std::string ntp = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ntp.c_str());
			DeviceConfig::getInstance()->set(INI_SECTION_SERVER, INI_KEY_NTP_IP, ntp);
		} else if (key == "Server_FS") {
			std::string fs_ip = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", fs_ip.c_str());
			DeviceConfig::getInstance()->set(INI_SECTION_SERVER, INI_KEY_FS_IP, fs_ip);
		} else if (key == "Server_FSPort") {
			std::string fs_port = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", fs_port.c_str());
			DeviceConfig::getInstance()->set(INI_SECTION_SERVER, INI_KEY_FS_PORT, fs_port);
		}
		// Env settings
		else if (key == "ENV_SD") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "ENV_SP") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "ENV_S") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "ENV_HT") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "ENV_IT") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "ENV_II") {
			std::string ptr = value.asString();
			Logger::log(LogLevel::INFO, " -> %s", ptr.c_str());
		} else if (key == "Status_ID") {
			int v = value.asInt();
			Logger::log(LogLevel::INFO, " -> %d", v);
		}
	}
}

/*
 *Function: Hex string to ascii directly
 *Purpose : Convert a hex string "12356789abcdef" to ascii "12 35 67 89 ab cd ef", the result length is half of the orignal.
 */
char *MgmtServClient::HTTPStrHToAscii(char *dest)
{
	uint32_t i, nn, digit;
	char *s = dest;
	i = nn = 0;
	do {
		if (isalnum((int)s[i])) {
			s[i] = toupper((int)s[i]);
			digit = (isalpha((int)s[i]) ? (s[i] - 'A' + 10) : s[i] - '0');
			if (digit > 15)
				break;
			s[i] = 0;
			dest[nn] |= (digit & 0xF) << (i % 2 ? 0 : 4);
			nn += i % 2;
		}
		i++;
	} while ((s[i] |= 0));

	return dest;
}

bool MgmtServClient::isAuthSuccess() {
    return auth_success.load();
}

int MgmtServClient::sendHeartbeat()
{
	std::string payload = formatHeartbeatMessage();
	Logger::log(LogLevel::DEBUG, "Heartbeat JSON: %s", payload.c_str());

	// spec：心跳走与 JPG 相同的 type-1 文件上传通道（服务器不处理 type=254，
	// 实测 type=254 5s 内无 ACK、服务器不入库）。文件名 = PID_YYYYMMDD_HHMMSS.JSON，
	// 落 /tmp 临时文件，upload() 后删除。newStorageServClient 共享本连接已 auth 的
	// socket_fd，upload() 内部构造 type-1 信封 + sendWithTimeout + 等 upload ACK
	//（回包走 handleUploadCommand，与 JPG 上传完全同流程）。
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	struct tm tm_local;
	localtime_r(&tv.tv_sec, &tm_local);
	char file_time[16];
	strftime(file_time, sizeof(file_time), "%Y%m%d_%H%M%S", &tm_local);
	auto pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	std::string tmp_path = std::string("/tmp/") + pid + "_" + file_time + ".JSON";

	std::ofstream out(tmp_path, std::ios::binary);
	if (!out.is_open()) {
		Logger::log(LogLevel::ERROR, "Heartbeat: write tmp file failed: %s", tmp_path.c_str());
		return EC_FAILED;
	}
	out << payload;
	out.close();

	Logger::log(LogLevel::INFO, "Heartbeat JSON file=%s size=%u",
		    tmp_path.c_str(), (unsigned)payload.length());

	auto storage = newStorageServClient();
	int rc = storage->upload(tmp_path);

	unlink(tmp_path.c_str());   // 清理临时文件（无论上传成败）
	return rc;
}

std::string MgmtServClient::formatHeartbeatMessage()
{
	struct timeval tv;
	gettimeofday(&tv, nullptr);
	// Use the reusable function to format current time with dynamic timezone
	std::string time_str = Timezone::getFormattedTimeWithTimezone(tv.tv_sec);
	Logger::log(LogLevel::DEBUG, "time %s", time_str.c_str());

	auto settings = Settings::getInstance();
	auto mcu = MCU::getInstance();

	Json::Value json_root;

	// Device information
	Json::Value device_info;
	device_info["PID"] = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	device_info["EUID"] = settings->euid;

	std::string ip_address = getIPAddress(getNetworkInterfaceName());
	Logger::log(LogLevel::DEBUG, "IP Address: %s", ip_address.c_str());
	device_info["IP"] = ip_address;

	device_info["GPS"] = mcu->readGps();

	auto battery1_volte = mcu->readBattery1Voltage();
	auto battery2_volte = mcu->readBattery2Voltage();
	device_info["Battery1"] = mcu->convertVoltage(battery1_volte);
	device_info["Battery2"] = mcu->convertVoltage(battery2_volte);

	auto external_volte = mcu->readExternalVoltage();
	auto shutdown_volte = mcu->readShutdownVoltage();
	auto lowpower_volte = mcu->readLowPowerVoltage();
	if (external_volte <= 14 || battery1_volte <= shutdown_volte) {
		device_info["SPower"] = "0";
		device_info["EPower"] = mcu->convertVoltage(external_volte);
	} else {
		device_info["EPower"] = "0";
		device_info["SPower"] = mcu->convertVoltage(external_volte);
	}

	auto disk_info = Disk::getInfo(DISK_PATHNAME);
	int used = (disk_info.total - disk_info.free) * 10 / 1024;
	int total = disk_info.total * 10 / 1024;
	std::ostringstream memory_stream;
	memory_stream << (used / 10) << "." << (used % 10) << "/" << (total / 10) << "." << (total % 10) << " G";
	device_info["Memory"] = memory_stream.str();
	device_info["WMode"] = 0;
	device_info["ONTime"] = settings->onTime_0 + (settings->onTime_1 << 8);
	device_info["NStatus"] = this->socket_fd > 0 ? 1 : 0;

	// spec：心跳固定报 AStatus=10
	device_info["AStatus"] = 10;

	auto battery_level = mcu->readBatteryLevel();
	device_info["BAT1_Level"] = battery_level;
	device_info["Low_PWR_Val"] = mcu->convertVoltage(lowpower_volte);
	device_info["Loff_PWR_Val"] = mcu->convertVoltage(shutdown_volte);
	device_info["UTime"] = time_str;
	json_root["device"] = device_info;

	// Data
	Json::Value data_info;
	data_info["D_Temperature"] =  to_string_custom(mcu->readTemperature());
	data_info["D_Humidity"] = to_string_custom(mcu->readHumidity());
	data_info["D_Atmos"] = to_string_custom(mcu->readAtmosPressure());

	json_root["data"] = data_info;

	// Network — T25 Phase-3: UPID migrated from DeviceConfig SYSTEM to Settings
	Json::Value network_info;
	auto upid = Settings::getInstance()->upid;
	network_info["N_UPID"] = upid;
	network_info["N_UIP"] = "0";
	network_info["N_CStatus"] = 0;
	network_info["N_CIP"] = "0";
	network_info["N_MStatus"] = 0;
	network_info["N_MIP"] = "0";

	json_root["network"] = network_info;

	// Signal
	Json::Value signal_info;
	auto IsWifiStationReady = mcu->IsWifiStationReady();
	if (IsWifiStationReady) {
		signal_info["S_CF"] = mcu->readSignalCF();
		signal_info["S_RSSI"] = mcu->readSignalRSSI();
		signal_info["S_RL"] = 0;
		signal_info["S_RSRP"] = mcu->readSignalRSRP();
		signal_info["S_RSRQ"] = mcu->readSignalRSRQ();
		signal_info["S_SNR"] = mcu->readSignalSNR();
		signal_info["S_TD"] = mcu->readSignalTD();
		signal_info["S_TP"] = mcu->readSignalTP();
	} else {
		auto program_type = ProductConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0);
		if (PTYPE_USB_DONGLE == program_type && mcu->Is4gExist()) {
			signal_info["S_RSSI"] = mcu->readSignalRSSI();
			signal_info["S_CF"] = mcu->readSignalCF();
		} else {
			signal_info["S_RSSI"] = 0;
			signal_info["S_CF"] = 0;
		}

		signal_info["S_RL"] = 0;
		signal_info["S_RSRP"] = 0;
		signal_info["S_RSRQ"] = 0;
		signal_info["S_SNR"] = 0;
		signal_info["S_TD"] = 0;
		signal_info["S_TP"] = 0;
	}

	json_root["signal"] = signal_info;

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, json_root);

	return message;
}

int MgmtServClient::str2time(const std::string& str)
{
	return str2time(str.c_str());
}

int MgmtServClient::str2time(const char * str)
{
	int i = 0, j = 0;
	if ( str )
		sscanf( str, "%d:%d", &i, &j );
	else{
		i = 0, j = 0;
	}

	return i * 60 + j;
}

int MgmtServClient::str2week(const std::string& str)
{
	return str2week(str.c_str());
}

int MgmtServClient::str2week(char * str)
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
