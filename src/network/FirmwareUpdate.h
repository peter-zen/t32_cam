#ifndef FIRMWARE_UPDATE_H
#define FIRMWARE_UPDATE_H

#include <memory>
#include <stdint.h>
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <json/json.h>

namespace network {
class FirmwareUpdate {
    public:
	static std::shared_ptr<FirmwareUpdate> getInstance();
	using sendMessageCallback = std::function<int(int message_type, const std::string &message)>;
	void bindSendMessageCallback(sendMessageCallback callback);
	int requestFileList();
	bool processFileList(const Json::Value &response);
	int processFile(const std::string &filename, const std::shared_ptr<char> file_buffer, size_t file_size);
	virtual ~FirmwareUpdate();
    private:
	std::string formatFileListRequest();
	std::vector<int> parseFileListResponse(const Json::Value &response);
	std::string formatDownloadFileRequest(int file_id);
	void processThread();
	int resetUpdateMark();

    private:
	FirmwareUpdate(const FirmwareUpdate &) = delete;
	FirmwareUpdate &operator=(const FirmwareUpdate &) = delete;
	FirmwareUpdate();
	bool process_thread_run;
	std::shared_ptr<std::thread> process_thread;
	std::mutex download_mutex;
	std::condition_variable download_condition_variable;
	std::mutex file_list_mutex;
	std::vector<int> file_list;
	std::vector<std::string> play_file_list;
	bool download_next_file;
	sendMessageCallback send_message_callback;
};
}
#endif
