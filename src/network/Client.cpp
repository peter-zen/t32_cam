#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <ifaddrs.h>
#include <time.h>
#include <unistd.h>
#include <Common.h>
#include <cstdlib>
#include <cstring>
#include "Client.h"
#include "Logger.h"
#include <sys/time.h>
#include "StringConvert.h"
#include "Misc.h"

#define RECV_BUF_LEN (50*1024)
#define SEND_BUF_LEN (1024*1024)

using namespace network;

Client::Client(const std::string &address, int port)
{
    socket_fd = -1;
    recv_thread_run = false;
    server_address = address;
    server_port = port;
    recv_buffer_size = RECV_BUF_LEN;
    send_buffer_size = SEND_BUF_LEN;
    send_buffer = std::unique_ptr<char[]>(new char[send_buffer_size]);
    recv_buffer = std::unique_ptr<char[]>(new char[recv_buffer_size]);
}

Client::Client(int socket_fd)
{
	this->socket_fd = socket_fd;
    recv_thread_run = false;
    recv_buffer_size = RECV_BUF_LEN;
    send_buffer_size = SEND_BUF_LEN;
    send_buffer = std::unique_ptr<char[]>(new char[send_buffer_size]);
    recv_buffer = std::unique_ptr<char[]>(new char[recv_buffer_size]);
}

Client::~Client()
{
	stop();
	close(socket_fd);
	socket_fd = -1;
}

int Client::connect(unsigned int timeout_ms)
{
    int ret = 0;
    struct sockaddr_in serv_addr;
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    // 使用 getaddrinfo 进行 DNS 解析
    if ((ret = getaddrinfo(server_address.c_str(), to_string_custom(server_port).c_str(), &hints, &res)) != 0) {
        Logger::log(LogLevel::ERROR, "getaddrinfo failed: %s", gai_strerror(ret));
        return EC_FAILED;
    }

    for (p = res; p != nullptr; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            // 获取 IP 地址
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
            Logger::log(LogLevel::INFO, "Resolve %s to IP: %s", server_address.c_str(), ip_str);

            memcpy(&serv_addr, p->ai_addr, sizeof(serv_addr));
            break;
        }
    }

    if (p == nullptr) {
        Logger::log(LogLevel::ERROR, "No valid address found");
        freeaddrinfo(res);
        return EC_FAILED;
    }

    freeaddrinfo(res);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(static_cast<uint16_t>(server_port));

    socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (timeout_ms > 0) {
        struct timeval start, current;
        // 获取开始时间
        gettimeofday(&start, nullptr);
        long long start_ms = static_cast<long long>(start.tv_sec) * 1000 + start.tv_usec / 1000;

        while (true) {
            // 获取当前时间
            gettimeofday(&current, nullptr);
            long long current_ms = static_cast<long long>(current.tv_sec) * 1000 + current.tv_usec / 1000;

            if (current_ms - start_ms >= timeout_ms) {
                break;
            }

            if (socket_fd != -1 || (socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1) {
                if ((ret = ::connect(socket_fd, reinterpret_cast<struct sockaddr *>(&serv_addr),
                                     sizeof(serv_addr))) != -1) {
                    break;
                }

                close(socket_fd);
                socket_fd = -1;
            }
            Logger::log(LogLevel::INFO, "Reconnecting server...");
            usleep(1000000);
        }
    } else {
        ret = ::connect(socket_fd, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));
    }

    if (ret == -1) {
        Logger::log(LogLevel::ERROR, "Connect to server(%s:%d) failed!", server_address.c_str(), server_port);
        return EC_FAILED;
    }

    int keep_alive = 1;
    int keep_idle = 2;
    int keep_interval = 1;
    int keep_count = 5;
    int keep_nodelay = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &keep_alive, sizeof(keep_alive))) {
        Logger::log(LogLevel::ERROR, "Error setsockopt(SO_KEEPALIVE) failed");
        return EC_FAILED;
    }
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep_idle, sizeof(keep_idle))) {
        Logger::log(LogLevel::ERROR, "Error setsockopt(TCP_KEEPIDLE) failed");
        return EC_FAILED;
    }
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep_interval, sizeof(keep_interval))) {
        Logger::log(LogLevel::ERROR, "Error setsockopt(TCP_KEEPINTVL) failed");
        return EC_FAILED;
    }
    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT, &keep_count, sizeof(keep_count))) {
        Logger::log(LogLevel::ERROR, "Error setsockopt(TCP_KEEPCNT) failed");
        return EC_FAILED;
    }

    if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &keep_nodelay, sizeof(keep_nodelay))) {
        Logger::log(LogLevel::ERROR, "Error setsockopt(TCP_NODELAY) failed");
        return EC_FAILED;
    }

    Logger::log(LogLevel::INFO, "Socket(%s:%d) init ok, %d", server_address.c_str(), server_port, socket_fd);
	return start();
}

int Client::start()
{
	recv_thread_run = true;
	recv_thread = std::make_shared<std::thread>(&Client::receiveFunction, this);
	Logger::log(LogLevel::INFO, "Client: start, socket_fd=%d", socket_fd);
	return EC_SUCCESS;
}

int Client::stop()
{
	recv_thread_run = false;
	if (recv_thread && recv_thread->joinable()) {
		recv_thread->join();
	}
	return EC_SUCCESS;
}

int Client::sendMessage(int message_type, const std::string &message)
{
	int total_length = 0;
	int ret = 0;
	int fixed_length = 12;
	int message_length = message.length();

	Logger::log(LogLevel::INFO, "%s length: %d, json: %s", __func__, message_length, message.c_str());
	total_length = fixed_length + message_length;

	memcpy(&send_buffer[0], &total_length, 4);
	memcpy(&send_buffer[4], &message_type, 4);
	memcpy(&send_buffer[8], &message_length, 4);
	memcpy(&send_buffer[12], message.c_str(), message_length);

	ret = send(socket_fd, send_buffer.get(), total_length, 0);
	Logger::log(LogLevel::DEBUG, "sendMessage result %d", ret);
	if (ret < 0) {
		Logger::log(LogLevel::ERROR, "%s: Error sending data", __func__);
		return EC_FAILED;
	}

	return EC_SUCCESS;
}

void Client::receiveFunction()
{
	fd_set read_fds;
	struct timeval timeout;
	int ret;

	while (recv_thread_run) {
		FD_ZERO(&read_fds);
		FD_SET(socket_fd, &read_fds);

		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;

		if (select(socket_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) {
			continue;
		}

		if (!FD_ISSET(socket_fd, &read_fds)) {
			continue;
		}

		if (receiveCommand(recv_buffer.get(), recv_buffer_size)) {
			processCommand(recv_buffer, ret);
		}
	}
}

int Client::receive(char *buffer, size_t length, int flags)
{
	int ret = recv(socket_fd, buffer, length, flags);
	if (ret < 0) {
		Logger::log(LogLevel::ERROR, "Error receiving data");
	}
	return ret;
}

int Client::receiveCommand(char *buffer, size_t length)
{
	// receive header
	size_t headerLen = 12;
	memset(buffer, 0, length);
	int ret = receive(buffer, headerLen, 0);

	if (headerLen == ret) {
		int message_length = 0;
		memcpy(&message_length, &buffer[8], 4);

		// receive json
		ret = receive(buffer + 12, message_length, 0);

		if (message_length == ret) {
			return message_length;
		}
	}

	return 0;
}

void Client::processCommand(std::unique_ptr<char[]> &buffer, size_t length)
{
	Logger::log(LogLevel::INFO, "processCommand");
}

std::string Client::getNetworkInterfaceName()
{
	return Misc::getNetworkInterfaceName();
}

void Client::setNetworkInterfaceName(std::string name)
{
	Misc::setNetworkInterfaceName(name);
}

std::string Client::getIPAddress(const std::string &interface_name)
{
	return Misc::getIPAddress(interface_name);
}