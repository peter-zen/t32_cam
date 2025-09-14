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
    protected:
	virtual int sendMessage(int message_type, const std::string &message);
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
};
}
#endif