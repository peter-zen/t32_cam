#include "Settings.h"
#include <mutex>
#include <json/json.h>
#include "Common.h"
#include "Settings.h"
#include "EnvManager.h"
#include <fstream>
#include "Broadcast.h"
#include "DeviceConfig.h"
#include "Logger.h"

using namespace network;

std::shared_ptr<Broadcast> Broadcast::getInstance()
{
	static std::shared_ptr<Broadcast> instance = nullptr;
	static std::once_flag flag;
	std::call_once(flag, []() { instance.reset(new Broadcast()); });
	return instance;
}

Broadcast::Broadcast()
{
	download_next_file = false;
	process_thread_run = true;
	process_thread = std::make_shared<std::thread>(&Broadcast::processThread, this);
}

Broadcast::~Broadcast()
{
	process_thread_run = false;
	if (process_thread && process_thread->joinable()) {
		process_thread->join();
	}
}
void Broadcast::bindSendMessageCallback(sendMessageCallback callback)
{
	send_message_callback = callback;
}

int Broadcast::requestFileList()
{
	auto request = formatFileListRequest();
	Logger::log(LogLevel::INFO, "request voice prompt list");
	return send_message_callback(MSG_TYPE_FILE_LIST, request);
}

bool Broadcast::processFileList(const Json::Value &response)
{
	std::unique_lock<std::mutex> lock(file_list_mutex);
	file_list = parseFileListResponse(response);
	if (file_list.empty()) {
		return false;
	}
	download_next_file = true;
	return true;
}

void Broadcast::processThread()
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

std::string Broadcast::formatFileListRequest()
{
	auto settings = Settings::getInstance();
	auto pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	Json::Value root;
	root["PID"] = pid;
	root["Comm_Code"] = settings->comm_code;
	root["FunType"] = 1;// 1: means get voice prompt file list

	Json::StreamWriterBuilder writer;
	std::string str_json = Json::writeString(writer, root);
	return str_json;
}

std::vector<int> Broadcast::parseFileListResponse(const Json::Value &response)
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

std::string Broadcast::formatDownloadFileRequest(int file_id)
{
	auto settings = Settings::getInstance();
	auto pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	Json::Value root;
	root["PID"] = pid;
	root["Comm_Code"] = settings->comm_code;
	root["FileFun"] = 1;
	root["FileNumber"] = file_id;

	Json::StreamWriterBuilder writer;
	std::string str_json = Json::writeString(writer, root);
	return str_json;
}

int Broadcast::processFile(const std::string &filename, const std::shared_ptr<char> file_buffer, size_t file_size)
{
	std::string file_pathname = EnvManager::getInstance()->getEnv("BROADCAST_FILE_PATH", "./") + filename;
	std::ofstream file(file_pathname, std::ios::binary);
	if (file.is_open()) {
		file.write(reinterpret_cast<const char*>(file_buffer.get()), static_cast<std::streamsize>(file_size));
		file.flush();
		file.close();
		play_file_list.push_back(file_pathname);
	}
	if (!file_list.empty()) {
		{
			std::unique_lock<std::mutex> lock(download_mutex);
			download_next_file = true;
			download_condition_variable.notify_one();
		}
		
	} else {
		Json::Value jsonRoot;
		jsonRoot["audioList"] = Json::arrayValue;
		Json::Value &jarray = jsonRoot["audioList"];

		for (auto& item: play_file_list) {
			jarray.append(item);
		}

		Json::StreamWriterBuilder writer;
		std::string data = Json::writeString(writer, jsonRoot);
		if (!data.empty()) {
			std::string file_pathname = EnvManager::getInstance()->getEnv("BROADCAST_FILELIST_PATHNAME",
										      "./AUDIO_PLAY_LIST.txt");

			std::ofstream file(file_pathname, std::ios::out | std::ios::binary | std::ios::trunc);
			if (file.is_open()) {
				file.write(data.c_str(), data.length());
				file.flush();
				file.close();
			}
			resetUpdateMark();
		}
	}

	return EC_SUCCESS;
}

int Broadcast::resetUpdateMark()
{
	auto settings = Settings::getInstance();
	Json::Value jsonRoot;
	jsonRoot["PID"] = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	jsonRoot["Comm_Code"] = settings->comm_code;

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, jsonRoot);
	return send_message_callback(MSG_TYPE_VOICE_BROADCAST_MARK, message);
}
