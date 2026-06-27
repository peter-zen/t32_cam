#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <ifaddrs.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <cerrno>
#include <Common.h>
#include <cstdlib>
#include <cstring>
#include "Client.h"
#include "Logger.h"
#include <sys/time.h>
#include <fcntl.h>
#include "utils/string/StringConvert.h"
#include "misc/Misc.h"

#define RECV_BUF_LEN (50*1024)
#define SEND_BUF_LEN (64*1024)

using namespace network;

Client::Client(const std::string &address, int port)
{
    Logger::log(LogLevel::INFO, "Client: ctor begin server=%s:%d", address.c_str(), port);
    socket_fd = -1;
    recv_thread_run = false;
    is_connected = false;
    owns_socket = true;
    server_address = address;
    server_port = port;
    recv_buffer_size = RECV_BUF_LEN;
    send_buffer_size = SEND_BUF_LEN;
    send_buffer = std::unique_ptr<char[]>(new char[send_buffer_size]);
    recv_buffer = std::unique_ptr<char[]>(new char[recv_buffer_size]);
    Logger::log(LogLevel::INFO, "Client: ctor done send_buf=%zu recv_buf=%zu",
                send_buffer_size, recv_buffer_size);
}

Client::Client(int socket_fd)
{
    Logger::log(LogLevel::INFO, "Client: ctor begin borrowed socket_fd=%d", socket_fd);
	this->socket_fd = socket_fd;
    recv_thread_run = false;
    is_connected = false;
    owns_socket = false;
    recv_buffer_size = RECV_BUF_LEN;
    send_buffer_size = SEND_BUF_LEN;
    send_buffer = std::unique_ptr<char[]>(new char[send_buffer_size]);
    recv_buffer = std::unique_ptr<char[]>(new char[recv_buffer_size]);
    Logger::log(LogLevel::INFO, "Client: ctor done send_buf=%zu recv_buf=%zu",
                send_buffer_size, recv_buffer_size);
}

Client::~Client()
{
	stop();
	if (owns_socket && socket_fd >= 0) {
		close(socket_fd);
	}
	socket_fd = -1;
}

void Client::shutdownSocket()
{
    is_connected = false;
    recv_thread_run = false;
    if (owns_socket && socket_fd >= 0) {
        ::shutdown(socket_fd, SHUT_RDWR);
    }
}

int Client::connect(unsigned int timeout_ms)
{
    int ret = 0;
    struct sockaddr_in serv_addr;
    struct addrinfo hints, *res, *p;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;

    Logger::log(LogLevel::INFO, "Client: resolving %s:%d", server_address.c_str(), server_port);
    if (std::getenv("RES_OPTIONS") == nullptr) {
        setenv("RES_OPTIONS", "attempts:1 timeout:1", 0);
    }

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

    // Bound send/recv blocking on this socket. Without SO_SNDTIMEO, ::send()
    // can block for the full TCP retransmit window (~13–30s, default
    // tcp_retries2=15) after the peer half-closes / RSTs. In devtest the mgmt
    // server rejects our PID/Sync_Key and closes the socket mid-handshake,
    // which used to wedge wm -m 1: the main thread blocked in
    // MgmtServClient::authenticate's send() so Power::requestShutdown() (and
    // the [wm] shutdown reason=*) lines never fired. 5s gives one MSS worth
    // of slack for the auth sendMessage (175B) and StorageServClient desc
    // uploads (~1KB JSON) while bounding the worst-case stall. Precedent:
    // src/service/event/TcpEventService.cpp:269-272 uses the same pattern.
    {
        struct timeval sendTimeout;
        sendTimeout.tv_sec = 5;
        sendTimeout.tv_usec = 0;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO,
                       &sendTimeout, sizeof(sendTimeout)) != 0) {
            Logger::log(LogLevel::WARNING,
                        "setsockopt(SO_SNDTIMEO) failed (errno=%d); send() may block",
                        errno);
        }
        struct timeval recvTimeout;
        recvTimeout.tv_sec = 10;
        recvTimeout.tv_usec = 0;
        if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO,
                       &recvTimeout, sizeof(recvTimeout)) != 0) {
            Logger::log(LogLevel::WARNING,
                        "setsockopt(SO_RCVTIMEO) failed (errno=%d); recv() may block",
                        errno);
        }
    }

    // ::connect() ignores SO_SNDTIMEO — its blocking time is governed by
    // kernel tcp_syn_retries (default 6, ~63s), which would blow past the
    // caller's timeout_ms retry loop on a half-broken peer. Mark the socket
    // non-blocking so ::connect() returns EINPROGRESS immediately; the retry
    // loop below uses select() to wait with the caller's deadline.
    {
        int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags == -1 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            Logger::log(LogLevel::WARNING,
                        "fcntl(O_NONBLOCK) failed (errno=%d); connect() may block",
                        errno);
        }
    }

    if (timeout_ms > 0) {
        struct timeval start, current;
        // 获取开始时间
        gettimeofday(&start, nullptr);
        long long start_ms = static_cast<long long>(start.tv_sec) * 1000 + start.tv_usec / 1000;

        while (true) {
            // 获取当前时间
            gettimeofday(&current, nullptr);
            long long current_ms = static_cast<long long>(current.tv_sec) * 1000 + current.tv_usec / 1000;

            long long remaining_ms = timeout_ms - (current_ms - start_ms);
            if (remaining_ms <= 0) {
                break;
            }

            if (socket_fd != -1 || (socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1) {
                // Re-apply O_NONBLOCK on the new socket: it was set above once,
                // but socket() inside the retry loop may have built a fresh fd.
                int flags = fcntl(socket_fd, F_GETFL, 0);
                if (flags != -1) fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);

                ret = ::connect(socket_fd, reinterpret_cast<struct sockaddr *>(&serv_addr),
                                sizeof(serv_addr));
                if (ret == 0) {
                    break;  // already connected (e.g. 127.0.0.1)
                }
                if (errno == EINPROGRESS) {
                    // Wait up to `remaining_ms` for the socket to become writable
                    // (= handshake completed or failed). SO_SNDTIMEO doesn't apply
                    // to ::connect(), and the kernel tcp_syn_retries default would
                    // otherwise let this block ~63s — that's what wedged wm -m 1.
                    struct timeval selectTv;
                    selectTv.tv_sec = remaining_ms / 1000;
                    selectTv.tv_usec = (remaining_ms % 1000) * 1000;
                    fd_set wfds;
                    FD_ZERO(&wfds);
                    FD_SET(socket_fd, &wfds);
                    int sel = select(socket_fd + 1, nullptr, &wfds, nullptr, &selectTv);
                    if (sel > 0) {
                        int soErr = 0;
                        socklen_t soLen = sizeof(soErr);
                        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &soErr, &soLen) == 0
                            && soErr == 0) {
                            ret = 0;
                            break;
                        }
                        Logger::log(LogLevel::WARNING,
                                    "connect failed async (so_error=%d)", soErr);
                        ret = -1;
                    } else if (sel == 0) {
                        Logger::log(LogLevel::WARNING,
                                    "connect timeout after %lld ms", (long long)remaining_ms);
                        ret = -1;
                    } else {
                        Logger::log(LogLevel::WARNING,
                                    "select on connect failed (errno=%d)", errno);
                        ret = -1;
                    }
                }

                if (ret == 0) break;

                close(socket_fd);
                socket_fd = -1;
            }
            Logger::log(LogLevel::INFO, "Reconnecting server...");
            usleep(1000000);
        }
    } else {
        ret = ::connect(socket_fd, reinterpret_cast<struct sockaddr *>(&serv_addr), sizeof(serv_addr));
    }

    // Whether connect succeeded via the EINPROGRESS+select path or the legacy
    // blocking path, flip the socket back to blocking so subsequent ::send /
    // ::recv honor SO_SNDTIMEO / SO_RCVTIMEO instead of returning EAGAIN.
    if (socket_fd != -1 && ret == 0) {
        int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags != -1) fcntl(socket_fd, F_SETFL, flags & ~O_NONBLOCK);
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
    is_connected = true;
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
	is_connected = false;
	recv_thread_run = false;
    if (owns_socket && socket_fd >= 0) {
        ::shutdown(socket_fd, SHUT_RDWR);
    }
	if (recv_thread && recv_thread->joinable()) {
		recv_thread->join();
	}
	return EC_SUCCESS;
}

bool Client::isConnected()
{
    return is_connected;
}

int Client::sendMessage(int message_type, const std::string &message)
{
	int total_length = 0;
	int ret = 0;
	int fixed_length = 12;
	int message_length = message.length();

	total_length = fixed_length + message_length;

	memcpy(&send_buffer[0], &total_length, 4);
	memcpy(&send_buffer[4], &message_type, 4);
	memcpy(&send_buffer[8], &message_length, 4);
	memcpy(&send_buffer[12], message.c_str(), message_length);

	ret = sendWithTimeout(socket_fd, send_buffer.get(), total_length, 5);
	Logger::log(LogLevel::INFO, "%s type=%d length=%d result=%d",
	            __func__, message_type, message_length, ret);
	Logger::log(LogLevel::DEBUG, "sendMessage result %d", ret);
	if (ret < 0) {
		Logger::log(LogLevel::ERROR, "%s: Error sending data", __func__);
		return EC_FAILED;
	}

	return EC_SUCCESS;
}

int Client::sendWithTimeout(int fd, const void *buf, size_t len, int timeoutSec)
{
	// Send may block for the full TCP retransmit window if the peer has
	// half-closed (FIN) without RST and our socket has no SO_SNDTIMEO. The
	// T32 uClibc kernel appears to ignore SO_SNDTIMEO in some flows (we still
	// saw send() block for 14s+ after setting it), so defend send() with
	// O_NONBLOCK + select(timeoutSec). Reused by Client::sendMessage (control
	// frames) and StorageServClient::upload (file payload). Restores the
	// socket's blocking flag on exit so SO_SNDTIMEO/RCVTIMEO still apply to
	// subsequent recv() and the receiveFunction loop.
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags != -1) fcntl(fd, F_SETFL, flags | O_NONBLOCK);

	const char *cursor = static_cast<const char *>(buf);
	size_t sent = 0;
	while (sent < len) {
		fd_set wfds;
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);
		struct timeval sendTv;
		sendTv.tv_sec = timeoutSec;
		sendTv.tv_usec = 0;
		int sel = select(fd + 1, nullptr, &wfds, nullptr, &sendTv);
		if (sel <= 0) {
			Logger::log(LogLevel::WARNING,
						"sendWithTimeout select timeout/error (fd=%d len=%zu sent=%zu sel=%d errno=%d)",
						fd, len, sent, sel, errno);
			if (flags != -1) fcntl(fd, F_SETFL, flags);
			return -1;
		}

		int ret = ::send(fd, cursor + sent, len - sent, 0);
		if (ret <= 0) {
			Logger::log(LogLevel::WARNING,
						"sendWithTimeout send failed (fd=%d len=%zu sent=%zu ret=%d errno=%d)",
						fd, len, sent, ret, errno);
			if (flags != -1) fcntl(fd, F_SETFL, flags);
			return -1;
		}
		sent += static_cast<size_t>(ret);
	}
	if (flags != -1) fcntl(fd, F_SETFL, flags);
	return static_cast<int>(sent);
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

		ret = receiveCommand(recv_buffer.get(), recv_buffer_size);
		if (ret > 0) {
			processCommand(recv_buffer, ret);
		} else {
			is_connected = false;
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
