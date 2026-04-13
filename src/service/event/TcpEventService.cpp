#include "TcpEventService.h"

#include <elog.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <sstream>

#define TAG "EVENT"

namespace service {

namespace {

constexpr uint64_t kHeartbeatLoopSleepMs = 200;

uint64_t nowMilliseconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

bool sendAll(int fd, const std::string& payload) {
    size_t totalSent = 0;
    while (totalSent < payload.size()) {
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        ssize_t sent = send(fd, payload.data() + totalSent, payload.size() - totalSent, flags);
        if (sent <= 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        totalSent += static_cast<size_t>(sent);
    }
    return true;
}

TcpEventMessage buildHeartbeatMessage() {
    TcpEventMessage message;
    message.category = "device";
    message.type = "device.heartbeat";
    message.level = TcpEventLevel::INFO;
    message.data["heartbeat_interval_ms"] = static_cast<Json::UInt64>(kDefaultTcpHeartbeatIntervalMs);
    return message;
}

} // namespace

std::shared_ptr<TcpEventService> TcpEventService::getInstance() {
    static std::shared_ptr<TcpEventService> instance(new TcpEventService());
    return instance;
}

TcpEventService::~TcpEventService() {
    stop();
}

bool TcpEventService::start(uint16_t port) {
    if (port == 0) {
        port = kDefaultTcpEventPort;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_) {
            if (port_ == port) {
                return true;
            }
        }
    }

    stop();

    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        elog_e(TAG, "failed to create listen socket: errno=%d", errno);
        return false;
    }

    int reuse = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        elog_e(TAG, "failed to bind port %u: errno=%d", port, errno);
        close(listenFd);
        return false;
    }

    if (listen(listenFd, 1) != 0) {
        elog_e(TAG, "failed to listen on port %u: errno=%d", port, errno);
        close(listenFd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        listenFd_ = listenFd;
        port_ = port;
        connectionSequence_ = 0;
        lastActivityMs_ = 0;
        running_ = true;
    }

    acceptThread_ = std::thread(&TcpEventService::acceptLoop, this);
    heartbeatThread_ = std::thread(&TcpEventService::heartbeatLoop, this);
    elog_i(TAG, "tcp event server started on port %u", port);
    return true;
}

void TcpEventService::stop() {
    int listenFd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && listenFd_ < 0 && clientFd_ < 0) {
            return;
        }
        running_ = false;
        listenFd = listenFd_;
        listenFd_ = -1;
    }

    if (listenFd >= 0) {
        close(listenFd);
    }

    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        closeClientLocked();
        connectionSequence_ = 0;
        lastActivityMs_ = 0;
    }

    elog_i(TAG, "tcp event server stopped");
}

bool TcpEventService::isRunning() const {
    return running_;
}

bool TcpEventService::hasClient() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clientFd_ >= 0;
}

uint16_t TcpEventService::port() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return port_;
}

bool TcpEventService::publish(const TcpEventMessage& message) {
    const uint64_t timestampMs = nowMilliseconds();
    const uint64_t timestampSeconds = timestampMs / 1000;

    std::lock_guard<std::mutex> lock(mutex_);
    return publishLocked(message, timestampSeconds, timestampMs);
}

bool TcpEventService::publishLocked(const TcpEventMessage& message,
                                    uint64_t timestampSeconds,
                                    uint64_t timestampMs) {
    Json::Value root(Json::objectValue);
    root["version"] = 1;
    root["event_id"] = buildEventId();
    root["category"] = message.category;
    root["type"] = message.type;
    root["timestamp"] = static_cast<Json::UInt64>(timestampSeconds);
    root["level"] = levelToString(message.level);
    root["data"] = message.data;

    Json::FastWriter writer;

    if (!running_ || clientFd_ < 0) {
        return false;
    }

    root["sequence"] = static_cast<Json::UInt64>(connectionSequence_ + 1);

    std::string payload = writer.write(root);
    if (payload.empty() || payload[payload.size() - 1] != '\n') {
        payload.push_back('\n');
    }

    if (!sendAll(clientFd_, payload)) {
        elog_w(TAG, "failed to send event %s, dropping client", message.type.c_str());
        closeClientLocked();
        return false;
    }

    ++connectionSequence_;
    lastActivityMs_ = timestampMs;
    return true;
}

void TcpEventService::acceptLoop() {
    while (running_) {
        int listenFd = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            listenFd = listenFd_;
        }

        if (listenFd < 0) {
            break;
        }

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listenFd, &readfds);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 200 * 1000;

        int ready = select(listenFd + 1, &readfds, NULL, NULL, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running_) {
                elog_w(TAG, "select failed on event server: errno=%d", errno);
            }
            continue;
        }

        if (ready == 0 || !FD_ISSET(listenFd, &readfds)) {
            continue;
        }

        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running_) {
                elog_w(TAG, "accept failed on event server: errno=%d", errno);
            }
            continue;
        }

        timeval sendTimeout;
        sendTimeout.tv_sec = 1;
        sendTimeout.tv_usec = 0;
        setsockopt(clientFd, SOL_SOCKET, SO_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ip, sizeof(ip));
        elog_i(TAG, "event client connected: %s:%u", ip, ntohs(clientAddr.sin_port));

        std::lock_guard<std::mutex> lock(mutex_);
        replaceClientLocked(clientFd);
    }
}

void TcpEventService::heartbeatLoop() {
    while (running_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (running_ && clientFd_ >= 0) {
                const uint64_t timestampMs = nowMilliseconds();
                if (lastActivityMs_ == 0 || timestampMs - lastActivityMs_ >= kDefaultTcpHeartbeatIntervalMs) {
                    publishLocked(buildHeartbeatMessage(), timestampMs / 1000, timestampMs);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kHeartbeatLoopSleepMs));
    }
}

void TcpEventService::closeClientLocked() {
    if (clientFd_ >= 0) {
        close(clientFd_);
        clientFd_ = -1;
    }
    lastActivityMs_ = 0;
}

void TcpEventService::replaceClientLocked(int clientFd) {
    closeClientLocked();
    clientFd_ = clientFd;
    connectionSequence_ = 0;
    lastActivityMs_ = 0;
}

std::string TcpEventService::buildEventId() const {
    const uint64_t counter = eventCounter_.fetch_add(1) + 1;
    std::ostringstream oss;
    oss << "evt_" << nowMilliseconds() << "_" << counter;
    return oss.str();
}

const char* TcpEventService::levelToString(TcpEventLevel level) {
    switch (level) {
        case TcpEventLevel::WARN:
            return "warn";
        case TcpEventLevel::ERROR:
            return "error";
        case TcpEventLevel::INFO:
        default:
            return "info";
    }
}

} // namespace service
