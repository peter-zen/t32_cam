// MgmtServClient.h
#ifndef MANAGE_SERVICE_CLIENT_H
#define MANAGE_SERVICE_CLIENT_H
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <condition_variable>
#include <json/json.h>
#include "StorageServClient.h"
#include "Rtmp.h"
#include "Client.h"

namespace network {
class MgmtServClient : public Client {
public:
	MgmtServClient(const std::string &address, int port);
	~MgmtServClient();

public:
	int authenticate();
	int sendHeartbeat();
	std::shared_ptr<StorageServClient> newStorageServClient();
	// recv_thread 异步处理 server resp 后会设 auth_success=true。
	// 结果用 atomic 存储，避免接收线程在认证完成路径上被等待线程的 mutex 状态卡住。
	bool isAuthSuccess();

private:
	int receiveCommand(char *buffer, size_t length);
	void processCommand(std::unique_ptr<char[]> &buffer, size_t length);
	void handleAuthCommand(const Json::Value &root);
	void handleFileListCommand(const Json::Value &root);
	void handleRtmpCommand(const Json::Value &root);
	void handleSettingCommand(const Json::Value &root);
	void handleDownloadFileCommand(const Json::Value &root);
	char *HTTPStrHToAscii(char *dest);
	int getCode(const char c);
	std::string generateSyncKey(const std::string &pid, int dev_type);
	std::string formatHeartbeatMessage();
	int str2time(const std::string& str);
	int str2time(const char * str);
	int str2week(const std::string& str);
	int str2week(char * str);

private:
	std::string sync_key;
	bool need_euid;
	bool remote_wakeup;
	int enable_broadcast;
	int enable_firmware_update;
	bool rtmp_ongoing;
	char rtmp_url[256];
	int rtmp_duration;
	std::shared_ptr<Rtmp> rtmp;
	std::mutex auth_mutex;
	std::condition_variable auth_cv;
	std::atomic<bool> auth_result_received;
	std::atomic<bool> auth_success;
	std::shared_ptr<StorageServClient> storage_serv_client;
};
}
#endif /* MANAGE_SERVICE_CLIENT_H */
