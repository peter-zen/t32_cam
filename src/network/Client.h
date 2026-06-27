#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <thread>
#include <mutex>
#include <memory>

namespace network {
class Client {
    public:
	Client(const std::string &address, int port);
	Client(int socket_fd);
	virtual ~Client();
	virtual int connect(unsigned int timeout_ms);
	virtual int start();
	virtual int stop();
	virtual bool isConnected();
	void shutdownSocket();

    protected:
	virtual int sendMessage(int message_type, const std::string &message);
	// O_NONBLOCK + select(timeoutSec) 守护的 ::send(),供派生类绕开
	// sendMessage 直接发裸字节时复用,避免在 mgmt server 半关/RST 后
	// 被 TCP 重传 (13–30s) 锁死。返回 send() 的字节数,< 0 表示失败。
	int sendWithTimeout(int fd, const void *buf, size_t len, int timeoutSec);
	virtual void receiveFunction();
	virtual int receive(char *buffer, size_t length, int flags);
	virtual int receiveCommand(char *buffer, size_t length);
	virtual void processCommand(std::unique_ptr<char[]> &buffer, size_t length);
	virtual std::string getNetworkInterfaceName();
	virtual void setNetworkInterfaceName(std::string name);
	virtual std::string getIPAddress(const std::string &interface_name);

    protected:
	std::string server_address;
	int server_port;
	int socket_fd;
	std::shared_ptr<std::thread> recv_thread;
	bool recv_thread_run;
	std::unique_ptr<char[]> send_buffer;
	std::unique_ptr<char[]> recv_buffer;
	size_t recv_buffer_size;
	size_t send_buffer_size;
	bool is_connected;
	bool owns_socket;
};
}
#endif
