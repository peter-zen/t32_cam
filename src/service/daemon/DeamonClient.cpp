// DeamonClient.cpp
// 守护客户端实现文件

#include "DeamonClient.h"
#include "Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

// 全局变量用于单例实现
static std::mutex instanceMutex;
static std::unique_ptr<DeamonClient> instance;

/**
 * 守护客户端实现类
 * 封装了与守护服务器通信的所有细节
 */
class DeamonClientImpl {
public:
    DeamonClientImpl() : clientSocket(-1), running(false), registered(false) {
    }
    
    ~DeamonClientImpl() {
        stop();
    }
    
    bool registerToServer(int pid, int intervalMs) {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (registered) {
            Logger::log(LogLevel::INFO, "Already registered to daemon server");
            return true;
        }
        
        // 保存注册信息
        this->pid = pid;
        this->intervalMs = intervalMs;
        
        // 连接到服务器
        if (!connectToServer()) {
            return false;
        }
        
        // 发送注册消息
        if (!sendRegisterMessage()) {
            close(clientSocket);
            clientSocket = -1;
            return false;
        }
        
        // 接收确认消息
        if (!receiveAckMessage()) {
            close(clientSocket);
            clientSocket = -1;
            return false;
        }
        
        registered = true;
        running = true;
        
        // 启动心跳线程
        heartbeatThread = std::thread(&DeamonClientImpl::heartbeatThreadFunc, this);
        
        Logger::log(LogLevel::INFO, "Successfully registered to daemon server");
        return true;
    }
    
    bool unregisterFromServer() {
        std::lock_guard<std::mutex> lock(mutex);
        
        if (!registered) {
            Logger::log(LogLevel::INFO, "Not registered to daemon server");
            return true;
        }
        
        stop();
        registered = false;
        
        Logger::log(LogLevel::INFO, "Successfully unregistered from daemon server");
        return true;
    }
    
    bool isConnectionActive() {
        std::lock_guard<std::mutex> lock(mutex);
        return registered && (clientSocket >= 0);
    }
    
    void stop() {
        running = false;
        
        // 等待心跳线程结束
        if (heartbeatThread.joinable()) {
            heartbeatThread.join();
        }
        
        // 关闭socket
        if (clientSocket >= 0) {
            close(clientSocket);
            clientSocket = -1;
        }
        
        registered = false;
    }
    
private:
    int clientSocket;
    int pid;
    int intervalMs;
    bool running;
    bool registered;
    std::thread heartbeatThread;
    std::mutex mutex;
    
    bool connectToServer() {
        // 创建socket
        clientSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (clientSocket < 0) {
            Logger::log(LogLevel::ERROR, "Failed to create socket");
            return false;
        }
        
        // 设置服务器地址
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(DAEMON_SERVER_PORT);
        
        // 尝试连接到服务器（假设服务器在本地）
        if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
            Logger::log(LogLevel::ERROR, "Invalid address");
            close(clientSocket);
            clientSocket = -1;
            return false;
        }
        
        // 连接服务器
        if (connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            Logger::log(LogLevel::ERROR, "Failed to connect to daemon server on port %d", DAEMON_SERVER_PORT);
            close(clientSocket);
            clientSocket = -1;
            return false;
        }
        
        return true;
    }
    
    bool sendRegisterMessage() {
        RegisterMessage msg;
        msg.type = MessageType::REGISTER;
        msg.pid = pid;
        msg.intervalMs = intervalMs;
        
        std::string data = msg.serialize();
        if (send(clientSocket, data.data(), data.size(), 0) != data.size()) {
            Logger::log(LogLevel::ERROR, "Failed to send register message");
            return false;
        }
        
        return true;
    }
    
    bool receiveAckMessage() {
        char buffer[sizeof(AckMessage)];
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(AckMessage), 0);
        
        if (bytesRead != sizeof(AckMessage)) {
            Logger::log(LogLevel::ERROR, "Failed to read ACK message");
            return false;
        }
        
        AckMessage msg;
        if (!msg.deserialize(std::string(buffer, sizeof(AckMessage)))) {
            Logger::log(LogLevel::ERROR, "Failed to deserialize ACK message");
            return false;
        }
        
        if (msg.type != MessageType::ACK || !msg.success) {
            Logger::log(LogLevel::ERROR, "Registration rejected by server");
            return false;
        }
        
        return true;
    }
    
    void heartbeatThreadFunc() {
        while (running) {
            // 等待心跳间隔
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs));
            
            if (!running) {
                break;
            }
            
            // 发送心跳包，最多重试一次
            for (int retry = 0; retry < 2; ++retry) {
                if (sendHeartbeat()) {
                    break; // 发送成功
                }
                
                // 如果重试失败，则等待下次间隔
                if (retry == 0) {
                    Logger::log(LogLevel::ERROR, "Failed to send heartbeat, retrying...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 短暂延迟后重试
                } else {
                    Logger::log(LogLevel::ERROR, "Heartbeat retry failed");
                }
            }
        }
    }
    
    bool sendHeartbeat() {
        HeartbeatMessage msg;
        msg.type = MessageType::HEARTBEAT;
        msg.pid = pid;
        
        std::string data = msg.serialize();
        
        // 使用mutex保护clientSocket的访问
        std::lock_guard<std::mutex> lock(mutex);
        
        if (clientSocket < 0) {
            return false;
        }
        
        if (send(clientSocket, data.data(), data.size(), 0) != data.size()) {
            return false;
        }
        
        return true;
    }
};

// DeamonClient类实现

DeamonClient::DeamonClient() : impl(std::make_unique<DeamonClientImpl>()) {
}

DeamonClient::~DeamonClient() {
    // std::unique_ptr会自动管理impl的生命周期，不需要手动delete
}

bool DeamonClient::registerToServer(int pid, int intervalMs) {
    return impl->registerToServer(pid, intervalMs);
}

bool DeamonClient::unregisterFromServer() {
    return impl->unregisterFromServer();
}

bool DeamonClient::isConnectionActive() {
    return impl->isConnectionActive();
}

void DeamonClient::stop() {
    impl->stop();
}