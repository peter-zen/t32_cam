#ifndef RTMP_H
#define RTMP_H

#include <string>
#include <functional>
#include <thread>
#include <memory>

namespace network {
class Rtmp {
    public:
	Rtmp(const std::string &url, int duration, std::function<void(int error_code)> callback);
	~Rtmp();
	bool start();
	bool stop();

    private:
	int rtmpUpload();
	void uploadFunction();
	std::string url;
	std::shared_ptr<std::thread> rtmp_thread;
	bool rtmp_thread_run;
	std::function<void(int error_code)> callback;
	int duration;
};
}
#endif
