#ifndef TCP_EVENT_SERVICE_H
#define TCP_EVENT_SERVICE_H

#include <json/json.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace service {

constexpr uint16_t kDefaultTcpEventPort = 5000;
constexpr uint64_t kDefaultTcpHeartbeatIntervalMs = 5000;

enum class TcpEventLevel {
    INFO,
    WARN,
    ERROR
};

struct TcpEventMessage {
    std::string category;
    std::string type;
    TcpEventLevel level = TcpEventLevel::INFO;
    Json::Value data = Json::objectValue;
};

class TcpEventService {
public:
    static std::shared_ptr<TcpEventService> getInstance();

    ~TcpEventService();

    bool start(uint16_t port = kDefaultTcpEventPort);
    void stop();

    bool isRunning() const;
    bool hasClient() const;
    uint16_t port() const;

    bool publish(const TcpEventMessage& message);

private:
    TcpEventService() = default;

    void acceptLoop();
    void heartbeatLoop();
    void closeClientLocked();
    bool publishLocked(const TcpEventMessage& message, uint64_t timestampSeconds, uint64_t timestampMs);
    void replaceClientLocked(int clientFd);
    std::string buildEventId() const;
    static const char* levelToString(TcpEventLevel level);

    mutable std::mutex mutex_;
    std::thread acceptThread_;
    std::thread heartbeatThread_;
    std::atomic<bool> running_{false};
    int listenFd_ = -1;
    int clientFd_ = -1;
    uint16_t port_ = kDefaultTcpEventPort;
    uint64_t connectionSequence_ = 0;
    uint64_t lastActivityMs_ = 0;
    mutable std::atomic<uint64_t> eventCounter_{0};
};

} // namespace service

#endif // TCP_EVENT_SERVICE_H
