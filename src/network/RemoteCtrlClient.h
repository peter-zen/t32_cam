#ifndef REMOTE_CTRL_CLIENT_H
#define REMOTE_CTRL_CLIENT_H

#include <string>
#include <thread>
#include <memory>
#include "Client.h"
#include "json/json.h"

namespace network {
class RemoteCtrlClient : public Client {
    public:
	RemoteCtrlClient(const std::string &address, int port);
	~RemoteCtrlClient();

    private:
	int receiveCommand(char *buffer, size_t length);
	void receiveFunction() override;
	void processCommand(std::unique_ptr<char[]> &buffer, size_t length);
	void handleGetParamAllCommand(const Json::Value &root);
	void handleSetParamCommand(const Json::Value &root);
	void handleResetParamCommand(const Json::Value &root);
	void handleSetDatetimeCommand(const Json::Value &root);
	void handleEnterWorkingModeCommand(const Json::Value &root);
	void handleGetHwInfoCommand(const Json::Value &root);
	void handleFormatSdcardCommand(const Json::Value &root);
	void handleGetSensorInfoCommand(const Json::Value &root);
	int str2time(const std::string& str);
	int str2time(const char * str);
	int str2week(const std::string& str);
	int str2week(char * str);

    private:
	bool remote_is_mobile_app;
	uint32_t vid_max_size = 8; // 8M
	uint32_t pic_max_size = 42;	// 42M
};
}
#endif // REMOTE_CTRL_CLIENT_H