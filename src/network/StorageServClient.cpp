#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <json/json.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include "CRC.h"
#include <iomanip>
#include <sstream>
#include <fstream>
#include "Logger.h"
#include "DeviceConfig.h"
#include "Settings.h"
#include "Common.h"
#include "Misc.h"
#include "StorageServClient.h"
#include "Timezone.h"

using namespace network;

StorageServClient::StorageServClient(const char *address, int port)
	: Client(address, port)
{
	upload_thread_run = true;
	upload_thread_started = false;
	upload_thread = std::make_shared<std::thread>(&StorageServClient::uploadThread, this);
	Logger::log(LogLevel::INFO, "StorageServClient: Constructor");
}

StorageServClient::StorageServClient(int socket_fd)
	: Client(socket_fd)
{
	Logger::log(LogLevel::INFO, "StorageServClient: Constructor");
	upload_thread_run = true;
	upload_thread_started = false;
	upload_thread = std::make_shared<std::thread>(&StorageServClient::uploadThread, this);
}

StorageServClient::~StorageServClient()
{
	upload_thread_run = false;
	if (upload_thread && upload_thread->joinable()) {
		upload_thread->join();
	}
	Logger::log(LogLevel::INFO, "StorageServClient: Destructor");
}

void StorageServClient::bindUploadCallback(UploadCallback callback)
{
	upload_callback = callback;
}

void StorageServClient::uploadThread()
{
	while (upload_thread_run) {
		upload_thread_started = true;
		if (socket_fd == -1 || upload_queue.empty()) {
			usleep(200000);
			continue;
		}
		std::unique_lock<std::mutex> lock(upload_queue_mutex);
		std::string file_path = upload_queue.front();
		lock.unlock();

		int error_code = upload(file_path);
		
		lock.lock();
		upload_queue.pop_front();
		lock.unlock();

		if (upload_callback) {
			upload_callback(file_path, error_code);
		}
	}
	upload_thread_started = false;
}

bool StorageServClient::isUploadFinished()
{
	return upload_queue.empty();
}

void StorageServClient::uploadFile(const std::string &filename)
{
	while (upload_thread_run && !upload_thread_started) {
	    usleep(10000);
	}

	std::unique_lock<std::mutex> lock(upload_queue_mutex);
	upload_queue.push_back(filename);
}

void StorageServClient::handleUploadCommand(const Json::Value &cmd)
{
	int status_id = cmd.get("Status_ID", 0).asInt();
	Logger::log(LogLevel::DEBUG, "Status_ID: %d", status_id);

	//notify upload result
	{
		std::lock_guard<std::mutex> lock(upload_mutex);
		upload_result_received = true;
		upload_success = (status_id == 0);
		upload_cv.notify_one();
	}
}

void StorageServClient::processCommand(int type, const Json::Value& cmd)
{
	switch (type) {
	case MSG_TYPE_UPLOAD_FILE:
		handleUploadCommand(cmd);
		break;
	default:
		Logger::log(LogLevel::WARNING, "Unknown message type: %d", type);
		break;
	}
}

int StorageServClient::upload(const std::string &file_pathname)
{
	struct timeval tv;
	gettimeofday(&tv, nullptr);

	// Use the reusable function to format current time with dynamic timezone
	std::string time_str = Timezone::getFormattedTimeWithTimezone(tv.tv_sec);

	uint16_t check_code = 0x0000;
	int ret = 0;
	int file_size = Misc::getFileSize(file_pathname);
	auto file_type = Misc::getFileType(file_pathname);
	auto file_name = Misc::getFilename(file_pathname);

	std::string pid = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
	auto comm_code = Settings::getInstance()->comm_code;
	// Package message
	Json::Value json_obj;
	json_obj["PID"] = pid;
	json_obj["Comm_Code"] = comm_code;
	json_obj["File"] = file_pathname;
	json_obj["FileType"] = file_type;
	json_obj["FileSize"] = file_size;
	json_obj["FileName"] = file_name;
	json_obj["Upload_Date"] = time_str;

	if (CRC::calculate_crc16(file_pathname, check_code) && check_code != 0x0000) {
		json_obj["F_CheckCode"] = check_code;
	}

	Json::StreamWriterBuilder writer;
	std::string message = Json::writeString(writer, json_obj);

	Logger::log(LogLevel::INFO, "Upload file info: %s", message.c_str());

	// Upload file
	int fixed_length = 12;
	int message_length = message.length();
	int total_length = fixed_length + message_length + file_size;
	int msg_type = MSG_TYPE_UPLOAD_FILE;

	memcpy(&send_buffer[0], &total_length, 4);
	memcpy(&send_buffer[4], &msg_type, 4);
	memcpy(&send_buffer[8], &message_length, 4);
	memcpy(&send_buffer[12], message.c_str(), message_length);

	std::ifstream file_stream;

	file_stream.open(file_pathname, std::ios::in | std::ios::binary);
	if (!file_stream.is_open()) {
		return EC_OPEN_FILE_FAILED;
	}

	auto start_time = std::chrono::steady_clock::now();

	if (total_length <= send_buffer_size) {
		file_stream.read(&send_buffer[fixed_length + message_length], file_size);
		ret = send(socket_fd, send_buffer.get(), total_length, 0);
	} else {
		int file_idx = send_buffer_size - fixed_length - message_length;
		file_stream.read(&send_buffer[fixed_length + message_length], file_idx);
		ret = send(socket_fd, send_buffer.get(), send_buffer_size, 0);

		while (ret >= 0 && file_idx < file_size) {
			file_stream.read(&send_buffer[0], send_buffer_size);
			int n_read = file_stream.gcount();
			if (n_read == 0)
				break;
			ret = send(socket_fd, send_buffer.get(), n_read, 0);
			file_idx += n_read;
		}
	}

	file_stream.close();

	if (ret < 0) {
		Logger::log(LogLevel::ERROR, "Upload file failed");
		return EC_UPLOAD_FILE_FAILED;
	}

	auto end_time = std::chrono::steady_clock::now();
	int duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
	Logger::log(LogLevel::INFO, "Upload file speed: %d kb/s, time: %d ms",
		    duration > 0 ? total_length / duration : 0, duration);

	//wait for response
	{
		std::unique_lock<std::mutex> lock(upload_mutex);
		upload_result_received = false;
		upload_success = false;
		upload_cv.wait_for(
			lock
			, std::chrono::seconds(10)
			, [this] { return upload_result_received;}
		);
	}

	return upload_success ? EC_SUCCESS : EC_FAILED;
}