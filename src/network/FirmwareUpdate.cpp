#include "Settings.h"
#include <mutex>
#include <json/json.h>
#include "Common.h"
#include "Settings.h"
#include "EnvManager.h"
#include <fstream>
#include "FirmwareUpdate.h"
#include "DeviceConfig.h"
#include "Logger.h"

#define CARD_ISP_FW_NAME "SPHOST.BRN"

using namespace network;

std::shared_ptr<FirmwareUpdate> FirmwareUpdate::getInstance()
{
	static std::shared_ptr<FirmwareUpdate> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new FirmwareUpdate()); });
	return instance;
}

FirmwareUpdate::FirmwareUpdate()
{
	download_next_file = false;
	process_thread_run = true;
	process_thread = std::make_shared<std::thread>(&FirmwareUpdate::processThread, this);
}

FirmwareUpdate::~FirmwareUpdate()
{
	process_thread_run = false;
	if (process_thread && process_thread->joinable()) {
		process_thread->join();
	}
}

void FirmwareUpdate::bindSendMessageCallback(sendMessageCallback callback)
{
	send_message_callback = callback;
}

int FirmwareUpdate::requestFileList()
{
	auto request = formatFileListRequest();
	Logger::log(LogLevel::INFO, "request firmware file list");
	return send_message_callback(MSG_TYPE_FILE_LIST, request);
}

bool FirmwareUpdate::processFileList(const Json::Value &response)
{
	std::unique_lock<std::mutex> lock(file_list_mutex);
	file_list = parseFileListResponse(response);
	if (file_list.empty()) {
		return false;
	}
	download_next_file = true;
	return true;
}

void FirmwareUpdate::processThread()
{
	while (process_thread_run) {
		if (file_list.empty() || !download_next_file) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
			continue;
		}
		
		for (auto &file_id : file_list) {
			std::string request;
			{
				std::unique_lock<std::mutex> lock(file_list_mutex);
				request = formatDownloadFileRequest(*file_list.begin());
				file_list.erase(file_list.begin());
				download_next_file = false;
			}
			send_message_callback(MSG_TYPE_DOWNLOAD_FILE, request);
			if (!file_list.empty()) {
				std::unique_lock<std::mutex> lock(download_mutex);
				auto status = download_condition_variable.wait_for(lock, std::chrono::seconds(5), [this]() { return download_next_file; });
				if (!status) {
					file_list.clear();
					break;
				}
			}
			
		}
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

}

std::string FirmwareUpdate::formatFileListRequest()
{
	auto settings = Settings::getInstance();
	auto pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	Json::Value root;
	root["PID"] = pid;
	root["Comm_Code"] = settings->comm_code;
	root["FunType"] = 0;//0: means get firmware file list

	Json::StreamWriterBuilder writer;
	std::string str_json = Json::writeString(writer, root);
	return str_json;
}

std::vector<int> FirmwareUpdate::parseFileListResponse(const Json::Value &response)
{
	std::vector<int> audio_update_file_list;
	auto file_list = response["FileList"];
	if (file_list.isArray()) {
		for (auto &file_id : file_list) {
			audio_update_file_list.push_back(file_id.asInt());
		}
	} else {
		audio_update_file_list.clear();
	}

	return audio_update_file_list;
}

std::string FirmwareUpdate::formatDownloadFileRequest(int file_id)
{
	auto settings = Settings::getInstance();
	auto pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	Json::Value root;
	root["PID"] = pid;
	root["Comm_Code"] = settings->comm_code;
	root["FileFun"] = 0;//0: means get firmware file
	root["FileNumber"] = file_id;

	Json::StreamWriterBuilder writer;
	std::string str_json = Json::writeString(writer, root);
	return str_json;
}

int FirmwareUpdate::processFile(const std::string &filename, const std::shared_ptr<char> file_buffer,
				size_t file_size)
{
	std::string file_path = EnvManager::getInstance()->getEnv("ISP_FILE_PATH", "./");
	std::string file_pathname = file_path + (filename.size() ? filename : CARD_ISP_FW_NAME);
	std::ofstream file(file_pathname, std::ios::binary);
	if (file.is_open()) {
		file.write(reinterpret_cast<const char*>(file_buffer.get()), static_cast<std::streamsize>(file_size));
		file.flush();
		file.close();
	}
	if (!file_list.empty()) {
		{
			std::unique_lock<std::mutex> lock(download_mutex);
			download_next_file = true;
			download_condition_variable.notify_one();
		}
		
	} else {
		resetUpdateMark();
	}

	return EC_SUCCESS;
}

int FirmwareUpdate::resetUpdateMark()
{
	auto settings = Settings::getInstance();
	Json::Value jsonRoot;
	jsonRoot["PID"] = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	jsonRoot["Comm_Code"] = settings->comm_code;

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, jsonRoot);
	return send_message_callback(MSG_TYPE_FIRMWARE_MARK, message);
}
