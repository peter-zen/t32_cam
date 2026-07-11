// StorageServClient.h
#ifndef STORAGE_SERVICE_CLIENT_H
#define STORAGE_SERVICE_CLIENT_H

#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <vector>
#include "Client.h"
#include <deque> // 包含 deque 头文件

namespace network {
class StorageServClient : public Client {
    public:
	StorageServClient(const char *address, int port);
	StorageServClient(int socket_fd);
	~StorageServClient();

	    public:
		using UploadCallback = std::function<void(const std::string &filename, int error_code)>;
		void bindUploadCallback(UploadCallback callback);
		void uploadFile(const std::string &filename);
		// 同步上传单文件：构造 type-1 信封 + sendWithTimeout + 等 upload ACK，
		// 返回 EC_SUCCESS/EC_FAILED。供 one-shot 调用方（如心跳）直接用；
		// 批量异步上传走 uploadFile + uploadThread。
		int upload(const std::string &file_pathname);
		void processCommand(int type, const Json::Value& cmd);
		bool isUploadFinished();
		void requestStop();

    private:
	void uploadThread();
	void handleUploadCommand(const Json::Value &cmd);

    private:
	UploadCallback upload_callback;
	std::shared_ptr<std::thread> upload_thread;
	bool upload_thread_run = false;
	std::deque<std::string> upload_queue;
	std::mutex upload_queue_mutex;
	std::condition_variable upload_queue_cv;
	bool upload_in_progress = false;

	std::mutex upload_mutex;
    std::condition_variable upload_cv;
	bool upload_result_received = false;
    bool upload_success = false;
};
}
#endif // STORAGE_SERVICE_CLIENT_H
