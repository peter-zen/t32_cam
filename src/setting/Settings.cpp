#include "Settings.h"
#include <mutex>
#include <fstream>
#include <cstring>
#include "Logger.h"

std::shared_ptr<Settings> Settings::getInstance()
{
	static std::shared_ptr<Settings> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new Settings()); });
	return instance;
}

bool Settings::saveToJsonFile(const std::string& filePath)
{
	// Create a JSON object
	Json::Value root;

	// Save uint8_t variables
	root["stillSize"] = static_cast<int>(this->stillSize);
	root["stillQuality"] = static_cast<int>(this->stillQuality);
	root["stillDriverMode"] = static_cast<int>(this->stillDriverMode);
	root["stillStamp"] = static_cast<int>(this->stillStamp);

	root["videoSize"] = static_cast<int>(this->videoSize);
	root["videoQuality"] = static_cast<int>(this->videoQuality);
	root["videoSeamless"] = static_cast<int>(this->videoSeamless);
	root["videoStamp"] = static_cast<int>(this->videoStamp);

	root["metering"] = static_cast<int>(this->metering);
	root["wb"] = static_cast<int>(this->wb);

	root["iso"] = static_cast<int>(this->iso);
	root["ev"] = static_cast<int>(this->ev);
	root["edge"] = static_cast<int>(this->edge);
	root["stablizer"] = static_cast<int>(this->stablizer);
	root["pvflickermode"] = static_cast<int>(this->pvflickermode);

	root["viddist"] = static_cast<int>(this->viddist);
	root["vidrsc"] = static_cast<int>(this->vidrsc);

	// ckvison settings
	root["cameraMode"] = static_cast<int>(this->cameraMode);
	root["realCameraMode"] = static_cast<int>(this->realCameraMode);
	root["burstNumber"] = static_cast<int>(this->burstNumber);
	root["shootingLimits"] = static_cast<int>(this->shootingLimits);
	root["videoLength_h"] = static_cast<int>(this->videoLength_h);
	root["videoLength_l"] = static_cast<int>(this->videoLength_l);

	root["pirEn"] = static_cast<int>(this->pirEn);
	root["ckPirSensitivity"] = static_cast<int>(this->ckPirSensitivity);
	root["trigInterval_h"] = static_cast<int>(this->trigInterval_h);
	root["trigInterval_m"] = static_cast<int>(this->trigInterval_m);
	root["trigInterval_s"] = static_cast<int>(this->trigInterval_s);

	root["timerEn"] = static_cast<int>(this->timerEn);
	root["timerLapse_h"] = static_cast<int>(this->timerLapse_h);
	root["timerLapse_m"] = static_cast<int>(this->timerLapse_m);
	root["timerLapse_s"] = static_cast<int>(this->timerLapse_s);

	root["timer1s_h"] = static_cast<int>(this->timer1s_h);
	root["timer1s_m"] = static_cast<int>(this->timer1s_m);
	root["timer1e_h"] = static_cast<int>(this->timer1e_h);
	root["timer1e_m"] = static_cast<int>(this->timer1e_m);

	root["timer2s_h"] = static_cast<int>(this->timer2s_h);
	root["timer2s_m"] = static_cast<int>(this->timer2s_m);
	root["timer2e_h"] = static_cast<int>(this->timer2e_h);
	root["timer2e_m"] = static_cast<int>(this->timer2e_m);

	root["timer3s_h"] = static_cast<int>(this->timer3s_h);
	root["timer3s_m"] = static_cast<int>(this->timer3s_m);
	root["timer3e_h"] = static_cast<int>(this->timer3e_h);
	root["timer3e_m"] = static_cast<int>(this->timer3e_m);

	root["weekRepeats"] = static_cast<int>(this->weekRepeats);
	root["stampEn"] = static_cast<int>(this->stampEn);
	root["autoCover"] = static_cast<int>(this->autoCover);

	root["showDevNameEn"] = static_cast<int>(this->showDevNameEn);
	root["pwdEn"] = static_cast<int>(this->pwdEn);

	root["onTime_0"] = static_cast<int>(this->onTime_0);
	root["onTime_1"] = static_cast<int>(this->onTime_1);

	root["heartRate_0"] = static_cast<int>(this->heartRate_0);
	root["heartRate_1"] = static_cast<int>(this->heartRate_1);
	root["heartRate_2"] = static_cast<int>(this->heartRate_2);
	root["heartRate_3"] = static_cast<int>(this->heartRate_3);

	root["lowVol_l"] = static_cast<int>(this->lowVol_l);
	root["lowVol_h"] = static_cast<int>(this->lowVol_h);

	root["endVol_l"] = static_cast<int>(this->endVol_l);
	root["endVol_h"] = static_cast<int>(this->endVol_h);

	root["uploadCnt"] = static_cast<int>(this->uploadCnt);

	root["gpsLatitudeRef"] = static_cast<int>(this->gpsLatitudeRef);
	root["gpsLatitude_0"] = static_cast<int>(this->gpsLatitude_0);
	root["gpsLatitude_1"] = static_cast<int>(this->gpsLatitude_1);
	root["gpsLatitude_2"] = static_cast<int>(this->gpsLatitude_2);
	root["gpsLatitude_3"] = static_cast<int>(this->gpsLatitude_3);

	root["gpsLongitudeRef"] = static_cast<int>(this->gpsLongitudeRef);
	root["gpsLongitude_0"] = static_cast<int>(this->gpsLongitude_0);
	root["gpsLongitude_1"] = static_cast<int>(this->gpsLongitude_1);
	root["gpsLongitude_2"] = static_cast<int>(this->gpsLongitude_2);
	root["gpsLongitude_3"] = static_cast<int>(this->gpsLongitude_3);

	root["gpsAltitudeRef"] = static_cast<int>(this->gpsAltitudeRef);
	root["gpsAltitude_0"] = static_cast<int>(this->gpsAltitude_0);
	root["gpsAltitude_1"] = static_cast<int>(this->gpsAltitude_1);
	root["gpsAltitude_2"] = static_cast<int>(this->gpsAltitude_2);
	root["gpsAltitude_3"] = static_cast<int>(this->gpsAltitude_3);

	// Save string variables
	root["devName"] = std::string(this->devName);
	root["devPwd"] = std::string(this->devPwd);

	root["bitRate_4k"] = static_cast<int>(this->bitRate_4k);
	root["bitRate_1080p"] = static_cast<int>(this->bitRate_1080p);
	root["bitRate_720p"] = static_cast<int>(this->bitRate_720p);
	root["isWLed"] = static_cast<int>(this->isWLed);
	root["continuous_record"] = static_cast<int>(this->continuous_record);
	root["remote_wakeup"] = static_cast<int>(this->remote_wakeup);

	// Save std::string variables
	root["comm_code"] = this->comm_code;
	root["euid"] = this->euid;
	root["duid"] = this->duid;

	// Save int variables
	root["setting_mark"] = this->setting_mark;
	root["enable_firmware_update"] = this->enable_firmware_update;

	// Write to file with pretty printing
	std::ofstream file(filePath);
	if (!file.is_open()) {
		Logger::log(LogLevel::ERROR, "Failed to open settings file for writing: %s", filePath.c_str());
		return false;
	}

	Json::StreamWriterBuilder writer;
	writer["indentation"] = "\t";
	std::unique_ptr<Json::StreamWriter> jsonWriter(writer.newStreamWriter());
	jsonWriter->write(root, &file);

	file.close();
	return true;
}

bool Settings::loadFromJsonFile(const std::string& filePath)
{

	// Read JSON from file
	std::ifstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	Json::Value root;
	Json::CharReaderBuilder reader;
	std::string errors;

	if (!Json::parseFromStream(reader, file, &root, &errors)) {
		file.close();
		return false;
	}

	file.close();

	// Load uint8_t variables
	if (root.isMember("stillSize")) this->stillSize = static_cast<uint8_t>(root["stillSize"].asInt());
	if (root.isMember("stillQuality")) this->stillQuality = static_cast<uint8_t>(root["stillQuality"].asInt());
	if (root.isMember("stillDriverMode")) this->stillDriverMode = static_cast<uint8_t>(root["stillDriverMode"].asInt());
	if (root.isMember("stillStamp")) this->stillStamp = static_cast<uint8_t>(root["stillStamp"].asInt());

	if (root.isMember("videoSize")) this->videoSize = static_cast<uint8_t>(root["videoSize"].asInt());
	if (root.isMember("videoQuality")) this->videoQuality = static_cast<uint8_t>(root["videoQuality"].asInt());
	if (root.isMember("videoSeamless")) this->videoSeamless = static_cast<uint8_t>(root["videoSeamless"].asInt());
	if (root.isMember("videoStamp")) this->videoStamp = static_cast<uint8_t>(root["videoStamp"].asInt());

	if (root.isMember("metering")) this->metering = static_cast<uint8_t>(root["metering"].asInt());
	if (root.isMember("wb")) this->wb = static_cast<uint8_t>(root["wb"].asInt());

	if (root.isMember("iso")) this->iso = static_cast<uint8_t>(root["iso"].asInt());
	if (root.isMember("ev")) this->ev = static_cast<uint8_t>(root["ev"].asInt());
	if (root.isMember("edge")) this->edge = static_cast<uint8_t>(root["edge"].asInt());
	if (root.isMember("stablizer")) this->stablizer = static_cast<uint8_t>(root["stablizer"].asInt());
	if (root.isMember("pvflickermode")) this->pvflickermode = static_cast<uint8_t>(root["pvflickermode"].asInt());

	if (root.isMember("viddist")) this->viddist = static_cast<uint8_t>(root["viddist"].asInt());
	if (root.isMember("vidrsc")) this->vidrsc = static_cast<uint8_t>(root["vidrsc"].asInt());

	// ckvison settings
	if (root.isMember("cameraMode")) this->cameraMode = static_cast<uint8_t>(root["cameraMode"].asInt());
	if (root.isMember("realCameraMode")) this->realCameraMode = static_cast<uint8_t>(root["realCameraMode"].asInt());
	if (root.isMember("burstNumber")) this->burstNumber = static_cast<uint8_t>(root["burstNumber"].asInt());
	if (root.isMember("shootingLimits")) this->shootingLimits = static_cast<uint8_t>(root["shootingLimits"].asInt());
	if (root.isMember("videoLength_h")) this->videoLength_h = static_cast<uint8_t>(root["videoLength_h"].asInt());
	if (root.isMember("videoLength_l")) this->videoLength_l = static_cast<uint8_t>(root["videoLength_l"].asInt());

	if (root.isMember("pirEn")) this->pirEn = static_cast<uint8_t>(root["pirEn"].asInt());
	if (root.isMember("ckPirSensitivity")) this->ckPirSensitivity = static_cast<uint8_t>(root["ckPirSensitivity"].asInt());
	if (root.isMember("trigInterval_h")) this->trigInterval_h = static_cast<uint8_t>(root["trigInterval_h"].asInt());
	if (root.isMember("trigInterval_m")) this->trigInterval_m = static_cast<uint8_t>(root["trigInterval_m"].asInt());
	if (root.isMember("trigInterval_s")) this->trigInterval_s = static_cast<uint8_t>(root["trigInterval_s"].asInt());

	if (root.isMember("timerEn")) this->timerEn = static_cast<uint8_t>(root["timerEn"].asInt());
	if (root.isMember("timerLapse_h")) this->timerLapse_h = static_cast<uint8_t>(root["timerLapse_h"].asInt());
	if (root.isMember("timerLapse_m")) this->timerLapse_m = static_cast<uint8_t>(root["timerLapse_m"].asInt());
	if (root.isMember("timerLapse_s")) this->timerLapse_s = static_cast<uint8_t>(root["timerLapse_s"].asInt());

	if (root.isMember("timer1s_h")) this->timer1s_h = static_cast<uint8_t>(root["timer1s_h"].asInt());
	if (root.isMember("timer1s_m")) this->timer1s_m = static_cast<uint8_t>(root["timer1s_m"].asInt());
	if (root.isMember("timer1e_h")) this->timer1e_h = static_cast<uint8_t>(root["timer1e_h"].asInt());
	if (root.isMember("timer1e_m")) this->timer1e_m = static_cast<uint8_t>(root["timer1e_m"].asInt());

	if (root.isMember("timer2s_h")) this->timer2s_h = static_cast<uint8_t>(root["timer2s_h"].asInt());
	if (root.isMember("timer2s_m")) this->timer2s_m = static_cast<uint8_t>(root["timer2s_m"].asInt());
	if (root.isMember("timer2e_h")) this->timer2e_h = static_cast<uint8_t>(root["timer2e_h"].asInt());
	if (root.isMember("timer2e_m")) this->timer2e_m = static_cast<uint8_t>(root["timer2e_m"].asInt());

	if (root.isMember("timer3s_h")) this->timer3s_h = static_cast<uint8_t>(root["timer3s_h"].asInt());
	if (root.isMember("timer3s_m")) this->timer3s_m = static_cast<uint8_t>(root["timer3s_m"].asInt());
	if (root.isMember("timer3e_h")) this->timer3e_h = static_cast<uint8_t>(root["timer3e_h"].asInt());
	if (root.isMember("timer3e_m")) this->timer3e_m = static_cast<uint8_t>(root["timer3e_m"].asInt());

	if (root.isMember("weekRepeats")) this->weekRepeats = static_cast<uint8_t>(root["weekRepeats"].asInt());
	if (root.isMember("stampEn")) this->stampEn = static_cast<uint8_t>(root["stampEn"].asInt());
	if (root.isMember("autoCover")) this->autoCover = static_cast<uint8_t>(root["autoCover"].asInt());

	if (root.isMember("showDevNameEn")) this->showDevNameEn = static_cast<uint8_t>(root["showDevNameEn"].asInt());
	if (root.isMember("pwdEn")) this->pwdEn = static_cast<uint8_t>(root["pwdEn"].asInt());

	if (root.isMember("onTime_0")) this->onTime_0 = static_cast<uint8_t>(root["onTime_0"].asInt());
	if (root.isMember("onTime_1")) this->onTime_1 = static_cast<uint8_t>(root["onTime_1"].asInt());

	if (root.isMember("heartRate_0")) this->heartRate_0 = static_cast<uint8_t>(root["heartRate_0"].asInt());
	if (root.isMember("heartRate_1")) this->heartRate_1 = static_cast<uint8_t>(root["heartRate_1"].asInt());
	if (root.isMember("heartRate_2")) this->heartRate_2 = static_cast<uint8_t>(root["heartRate_2"].asInt());
	if (root.isMember("heartRate_3")) this->heartRate_3 = static_cast<uint8_t>(root["heartRate_3"].asInt());

	if (root.isMember("lowVol_l")) this->lowVol_l = static_cast<uint8_t>(root["lowVol_l"].asInt());
	if (root.isMember("lowVol_h")) this->lowVol_h = static_cast<uint8_t>(root["lowVol_h"].asInt());

	if (root.isMember("endVol_l")) this->endVol_l = static_cast<uint8_t>(root["endVol_l"].asInt());
	if (root.isMember("endVol_h")) this->endVol_h = static_cast<uint8_t>(root["endVol_h"].asInt());

	if (root.isMember("uploadCnt")) this->uploadCnt = static_cast<uint8_t>(root["uploadCnt"].asInt());

	if (root.isMember("gpsLatitudeRef")) this->gpsLatitudeRef = static_cast<uint8_t>(root["gpsLatitudeRef"].asInt());
	if (root.isMember("gpsLatitude_0")) this->gpsLatitude_0 = static_cast<uint8_t>(root["gpsLatitude_0"].asInt());
	if (root.isMember("gpsLatitude_1")) this->gpsLatitude_1 = static_cast<uint8_t>(root["gpsLatitude_1"].asInt());
	if (root.isMember("gpsLatitude_2")) this->gpsLatitude_2 = static_cast<uint8_t>(root["gpsLatitude_2"].asInt());
	if (root.isMember("gpsLatitude_3")) this->gpsLatitude_3 = static_cast<uint8_t>(root["gpsLatitude_3"].asInt());

	if (root.isMember("gpsLongitudeRef")) this->gpsLongitudeRef = static_cast<uint8_t>(root["gpsLongitudeRef"].asInt());
	if (root.isMember("gpsLongitude_0")) this->gpsLongitude_0 = static_cast<uint8_t>(root["gpsLongitude_0"].asInt());
	if (root.isMember("gpsLongitude_1")) this->gpsLongitude_1 = static_cast<uint8_t>(root["gpsLongitude_1"].asInt());
	if (root.isMember("gpsLongitude_2")) this->gpsLongitude_2 = static_cast<uint8_t>(root["gpsLongitude_2"].asInt());
	if (root.isMember("gpsLongitude_3")) this->gpsLongitude_3 = static_cast<uint8_t>(root["gpsLongitude_3"].asInt());

	if (root.isMember("gpsAltitudeRef")) this->gpsAltitudeRef = static_cast<uint8_t>(root["gpsAltitudeRef"].asInt());
	if (root.isMember("gpsAltitude_0")) this->gpsAltitude_0 = static_cast<uint8_t>(root["gpsAltitude_0"].asInt());
	if (root.isMember("gpsAltitude_1")) this->gpsAltitude_1 = static_cast<uint8_t>(root["gpsAltitude_1"].asInt());
	if (root.isMember("gpsAltitude_2")) this->gpsAltitude_2 = static_cast<uint8_t>(root["gpsAltitude_2"].asInt());
	if (root.isMember("gpsAltitude_3")) this->gpsAltitude_3 = static_cast<uint8_t>(root["gpsAltitude_3"].asInt());

	// Load string variables
	if (root.isMember("devName")) {
		const std::string& devNameStr = root["devName"].asString();
		strncpy(this->devName, devNameStr.c_str(), sizeof(this->devName) - 1);
		this->devName[sizeof(this->devName) - 1] = '\0'; // Ensure null-terminated
	}

	if (root.isMember("devPwd")) {
		const std::string& devPwdStr = root["devPwd"].asString();
		strncpy(this->devPwd, devPwdStr.c_str(), sizeof(this->devPwd) - 1);
		this->devPwd[sizeof(this->devPwd) - 1] = '\0'; // Ensure null-terminated
	}

	if (root.isMember("bitRate_4k")) this->bitRate_4k = static_cast<uint8_t>(root["bitRate_4k"].asInt());
	if (root.isMember("bitRate_1080p")) this->bitRate_1080p = static_cast<uint8_t>(root["bitRate_1080p"].asInt());
	if (root.isMember("bitRate_720p")) this->bitRate_720p = static_cast<uint8_t>(root["bitRate_720p"].asInt());
	if (root.isMember("isWLed")) this->isWLed = static_cast<uint8_t>(root["isWLed"].asInt());
	if (root.isMember("continuous_record")) this->continuous_record = static_cast<uint8_t>(root["continuous_record"].asInt());
	if (root.isMember("remote_wakeup")) this->remote_wakeup = static_cast<uint8_t>(root["remote_wakeup"].asInt());

	// Load std::string variables
	if (root.isMember("comm_code")) this->comm_code = root["comm_code"].asString();
	if (root.isMember("euid")) this->euid = root["euid"].asString();
	if (root.isMember("duid")) this->duid = root["duid"].asString();

	// Load int variables
	if (root.isMember("setting_mark")) this->setting_mark = root["setting_mark"].asInt();
	if (root.isMember("enable_firmware_update")) this->enable_firmware_update = root["enable_firmware_update"].asInt();

	return true;
}
