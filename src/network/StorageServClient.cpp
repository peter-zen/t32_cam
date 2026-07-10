#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <json/json.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include "utils/crc/CRC.h"
#include <iomanip>
#include <sstream>
#include <fstream>
#include "Logger.h"
#include "DeviceConfig.h"
#include "Settings.h"
#include "Common.h"
#include "misc/Misc.h"
#include "StorageServClient.h"
#include "time/timezone/Timezone.h"

using namespace network;

StorageServClient::StorageServClient(const char *address, int port)
	: Client(address, port)
{
	upload_thread_run = true;
	upload_thread = std::make_shared<std::thread>(&StorageServClient::uploadThread, this);
	Logger::log(LogLevel::INFO, "StorageServClient: Constructor");
}

StorageServClient::StorageServClient(int socket_fd)
	: Client(socket_fd)
{
	Logger::log(LogLevel::INFO, "StorageServClient: Constructor");
	upload_thread_run = true;
	upload_thread = std::make_shared<std::thread>(&StorageServClient::uploadThread, this);
}

StorageServClient::~StorageServClient()
{
	requestStop();
	if (upload_thread && upload_thread->joinable()) {
		upload_thread->join();
	}
	Logger::log(LogLevel::INFO, "StorageServClient: Destructor");
}

void StorageServClient::requestStop()
{
	{
		std::lock_guard<std::mutex> lock(upload_queue_mutex);
		upload_thread_run = false;
	}
	upload_queue_cv.notify_all();
	{
		std::lock_guard<std::mutex> lock(upload_mutex);
		upload_result_received = true;
		upload_success = false;
	}
	upload_cv.notify_all();
	shutdownSocket();
}

void StorageServClient::bindUploadCallback(UploadCallback callback)
{
	upload_callback = callback;
}

void StorageServClient::uploadThread()
{
	while (true) {
		std::string file_path;
		{
			std::unique_lock<std::mutex> lock(upload_queue_mutex);
			upload_queue_cv.wait(lock, [this] {
				return !upload_thread_run || !upload_queue.empty();
			});
			if (!upload_thread_run) {
				break;
			}
			if (socket_fd == -1) {
				lock.unlock();
				usleep(200000);
				continue;
			}
			file_path = upload_queue.front();
			upload_queue.pop_front();
			upload_in_progress = true;
		}

		Logger::log(LogLevel::DEBUG, "StorageServClient: upload begin: %s", file_path.c_str());

		int error_code = EC_FAILED;
		try {
			error_code = upload(file_path);
		} catch (const std::exception &e) {
			Logger::log(LogLevel::ERROR, "StorageServClient: upload() threw on %s: %s",
			            file_path.c_str(), e.what());
		}

		if (upload_callback) {
			try {
				upload_callback(file_path, error_code);
			} catch (const std::exception &e) {
				Logger::log(LogLevel::ERROR, "StorageServClient: upload callback threw on %s: %s",
				            file_path.c_str(), e.what());
			}
		}

		{
			std::lock_guard<std::mutex> lock(upload_queue_mutex);
			upload_in_progress = false;
		}
		upload_queue_cv.notify_all();
	}
}

bool StorageServClient::isUploadFinished()
{
	std::lock_guard<std::mutex> lock(upload_queue_mutex);
	return upload_queue.empty() && !upload_in_progress;
}

void StorageServClient::uploadFile(const std::string &filename)
{
	{
		std::lock_guard<std::mutex> lock(upload_queue_mutex);
		if (!upload_thread_run) {
			Logger::log(LogLevel::WARNING, "StorageServClient: upload stopped, skip %s", filename.c_str());
			return;
		}
		upload_queue.push_back(filename);
		Logger::log(LogLevel::INFO, "StorageServClient: queued upload file: %s", filename.c_str());
	}
	upload_queue_cv.notify_one();
}

void StorageServClient::handleUploadCommand(const Json::Value &cmd)
{
	int status_id = cmd.get("Status_ID", 0).asInt();
	Logger::log(LogLevel::INFO, "Status_ID: %d", status_id);

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

	// Per-send timeout (seconds). Default 30s — the devtest mgmt server drains
	// large uploads via kernel TCP retransmit taking 13-30s; the old hardcoded 5s
	// falsely failed ~170KB JPGs at the first 64KB chunk. Override: HTC_UPLOAD_SEND_TIMEOUT_MS.
	int sendTimeoutSec = 30;
	if (const char *e = std::getenv("HTC_UPLOAD_SEND_TIMEOUT_MS")) {
		int v = std::atoi(e);
		if (v > 0) sendTimeoutSec = v / 1000;
	}

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

	{
		std::lock_guard<std::mutex> lock(upload_mutex);
		upload_result_received = false;
		upload_success = false;
	}

	// NOTE: StorageServClient::upload 走裸 ::send(),绕过 Client::sendMessage。
	// mgmt server 在 devtest 环境接受 auth(PID/secret 通过)后会主动关闭 socket;
	// 上传 JPG payload (72KB) 单次 send 超 send_buffer_size 阈值时即便单包也
	// 可能撞内核 TCP 重传 (13–30s) 把 upload 线程锁死。沿用 Client::sendMessage
	// 的 O_NONBLOCK + select(5s) 守护,统一所有 TCP send 路径的阻塞上界。
	if (total_length <= send_buffer_size) {
		file_stream.read(&send_buffer[fixed_length + message_length], file_size);
		ret = sendWithTimeout(socket_fd, send_buffer.get(), total_length, sendTimeoutSec);
	} else {
		int file_idx = send_buffer_size - fixed_length - message_length;
		file_stream.read(&send_buffer[fixed_length + message_length], file_idx);
		ret = sendWithTimeout(socket_fd, send_buffer.get(), send_buffer_size, sendTimeoutSec);

		while (ret >= 0 && file_idx < file_size) {
			file_stream.read(&send_buffer[0], send_buffer_size);
			int n_read = file_stream.gcount();
			if (n_read == 0)
				break;
			ret = sendWithTimeout(socket_fd, send_buffer.get(), n_read, sendTimeoutSec);
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

	// Wait for the server's upload ack (Status_ID response). Default 15s — a slow
	// uplink can need >5s for the server to ack a ~170KB JPG (observed 5.1s), which
	// the old hardcoded 5s falsely turned into EC_FAILED. Override with
	// HTC_UPLOAD_ACK_TIMEOUT_MS (milliseconds).
	int ackTimeoutMs = 15000;
	if (const char *e = std::getenv("HTC_UPLOAD_ACK_TIMEOUT_MS")) {
		int v = std::atoi(e);
		if (v > 0) ackTimeoutMs = v;
	}
	{
		std::unique_lock<std::mutex> lock(upload_mutex);
		upload_cv.wait_for(
			lock
			, std::chrono::milliseconds(ackTimeoutMs)
			, [this] { return upload_result_received;}
		);
	}

	return upload_success ? EC_SUCCESS : EC_FAILED;
}
