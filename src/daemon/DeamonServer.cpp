// DeamonServer.cpp
// 守护服务器实现文件

#include "DeamonServer.h"
#include "Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <chrono>
#include <atomic>
#include <thread>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include "Misc.h"

// 全局变量用于单例实现
static std::mutex instanceMutex;
static std::unique_ptr<DeamonServer> instance;

// DeamonServer类实现

DeamonServer& DeamonServer::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (!instance) {
        instance.reset(new DeamonServer());
    }
    return *instance;
}

DeamonServer::DeamonServer() : serverSocket(-1), running(false) {
}

DeamonServer::~DeamonServer() {
    stop();
}

bool DeamonServer::start() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    
    if (running) {
        Logger::log(LogLevel::INFO, "DeamonServer already running");
        return true;
    }
    
    // 创建socket
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        Logger::log(LogLevel::ERROR, "Failed to create socket");
        return false;
    }
    
    // 设置SO_REUSEADDR选项
    int reuseAddr = 1;
    if (setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &reuseAddr, sizeof(reuseAddr)) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to set SO_REUSEADDR");
        close(serverSocket);
        serverSocket = -1;
        return false;
    }
    
    // 绑定端口
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(DAEMON_SERVER_PORT);
    
    if (bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to bind to port %d", DAEMON_SERVER_PORT);
        close(serverSocket);
        serverSocket = -1;
        return false;
    }
    
    // 监听连接
    if (listen(serverSocket, 10) < 0) {
        Logger::log(LogLevel::ERROR, "Failed to listen on socket");
        close(serverSocket);
        serverSocket = -1;
        return false;
    }
    
    running = true;
    
    // 启动服务器线程和心跳监控线程
    serverThread = std::thread(&DeamonServer::serverMainLoop, this);
    monitorThread = std::thread(&DeamonServer::heartbeatMonitorThread, this);
    
    Logger::log(LogLevel::INFO, "DeamonServer started successfully on port %d", DAEMON_SERVER_PORT);
    return true;
}

void DeamonServer::stop() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    
    if (!running) {
        return;
    }
    
    running = false;
    
    // 关闭服务器socket，中断accept调用
    if (serverSocket >= 0) {
        close(serverSocket);
        serverSocket = -1;
    }
    
    // 关闭所有客户端连接
    {   
        std::lock_guard<std::mutex> clientsLock(clientsMutex);
        for (auto& client : clients) {
            close(client.first);
        }
        clients.clear();
    }
    
    // 等待线程结束
    if (serverThread.joinable()) {
        serverThread.join();
    }
    
    if (monitorThread.joinable()) {
        monitorThread.join();
    }
    
    Logger::log(LogLevel::INFO, "DeamonServer stopped");
}

bool DeamonServer::registerCallback(std::function<bool()> callback) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    shutdownCallback = callback;
    return true;
}

void DeamonServer::serverMainLoop() {
    while (running) {
        // 接受新连接
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        
        int clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if (clientSocket < 0) {
            if (running) { // 只有在服务器仍在运行时才输出错误
                Logger::log(LogLevel::ERROR, "Failed to accept connection");
            }
            continue;
        }
        
        // 为每个客户端创建新的处理线程
        std::thread clientThread(&DeamonServer::clientHandlerThread, this, clientSocket);
        clientThread.detach(); // 分离线程，让它自行管理生命周期
    }
}

void DeamonServer::clientHandlerThread(int clientSocket) {
    try {
        // 读取注册消息
        RegisterMessage registerMsg;
        char buffer[sizeof(RegisterMessage)];
        
        ssize_t bytesRead = recv(clientSocket, buffer, sizeof(RegisterMessage), 0);
        if (bytesRead != sizeof(RegisterMessage)) {
            Logger::log(LogLevel::ERROR, "Failed to read register message");
            close(clientSocket);
            return;
        }
        
        // 反序列化注册消息
        if (!registerMsg.deserialize(std::string(buffer, sizeof(RegisterMessage)))) {
            Logger::log(LogLevel::ERROR, "Failed to deserialize register message");
            close(clientSocket);
            return;
        }
        
        // 创建客户端信息
        ClientInfo clientInfo;
        clientInfo.socketFd = clientSocket;
        clientInfo.pid = registerMsg.pid;
        clientInfo.heartbeatIntervalMs = registerMsg.intervalMs;
        clientInfo.missingHeartbeatCount = 0;
        clientInfo.state = ClientState::CONNECTED;
        clientInfo.lastHeartbeatTime = std::chrono::steady_clock::now();
        
        // 将客户端添加到客户端列表
        {   
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients[clientSocket] = clientInfo;
        }
        
        Logger::log(LogLevel::INFO, "Client registered: PID=%d, interval=%dms", registerMsg.pid, registerMsg.intervalMs);
        
        // 发送确认消息
        AckMessage ackMsg;
        ackMsg.type = MessageType::ACK;
        ackMsg.success = true;
        
        std::string ackData = ackMsg.serialize();
        if (send(clientSocket, ackData.data(), ackData.size(), 0) != ackData.size()) {
            Logger::log(LogLevel::ERROR, "Failed to send ACK message");
            close(clientSocket);
            
            // 从客户端列表中移除
            {   
                std::lock_guard<std::mutex> lock(clientsMutex);
                clients.erase(clientSocket);
            }
            return;
        }
        
        // 处理客户端心跳消息
        while (running) {
            HeartbeatMessage heartbeatMsg;
            bytesRead = recv(clientSocket, buffer, sizeof(HeartbeatMessage), 0);
            
            if (bytesRead <= 0) {
                // 连接关闭或发生错误
                break;
            }
            
            if (bytesRead == sizeof(HeartbeatMessage)) {
                // 反序列化心跳消息
                if (heartbeatMsg.deserialize(std::string(buffer, sizeof(HeartbeatMessage)))) {
                    // 更新客户端心跳信息
                    {   
                        std::lock_guard<std::mutex> lock(clientsMutex);
                        auto it = clients.find(clientSocket);
                        if (it != clients.end()) {
                            it->second.missingHeartbeatCount = 0;
                            it->second.lastHeartbeatTime = std::chrono::steady_clock::now();
                            it->second.state = ClientState::CONNECTED;
                        }
                    }
                }
            }
        }
        
        // 客户端断开连接
        {   
            std::lock_guard<std::mutex> lock(clientsMutex);
            auto it = clients.find(clientSocket);
            if (it != clients.end()) {
                it->second.state = ClientState::DISCONNECTED;
                Logger::log(LogLevel::INFO, "Client disconnected: PID=%d", it->second.pid);
            }
        }
        
        close(clientSocket);
        
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERROR, "Exception in client handler: %s", e.what());
        close(clientSocket);
        
        // 从客户端列表中移除
        {   
            std::lock_guard<std::mutex> lock(clientsMutex);
            clients.erase(clientSocket);
        }
    }
}

void DeamonServer::heartbeatMonitorThread() {
    while (running) {
        // 等待一小段时间
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        bool allClientsDisconnected = true;
        
        {   
            std::lock_guard<std::mutex> lock(clientsMutex);
            
            auto now = std::chrono::steady_clock::now();
            
            // 检查所有客户端的心跳状态
            for (auto& client : clients) {
                if (client.second.state == ClientState::CONNECTED) {
                    // 计算自上次心跳以来的时间
                    auto timeSinceLastHeartbeat = 
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - client.second.lastHeartbeatTime).count();
                    
                    // 如果超过心跳间隔，则增加丢失计数
                    if (timeSinceLastHeartbeat > client.second.heartbeatIntervalMs) {
                        client.second.missingHeartbeatCount++;
                        
                        // 如果连续丢失3个心跳包，则标记为失联
                        if (client.second.missingHeartbeatCount >= MAX_MISSING_HEARTBEATS) {
                            client.second.state = ClientState::MISSING;
                            Logger::log(LogLevel::INFO, "Client marked as missing: PID=%d, missing heartbeats=%d", 
                                      client.second.pid, client.second.missingHeartbeatCount);
                        }
                    }
                    
                    // 如果客户端仍处于连接状态，则系统不应该关闭
                    allClientsDisconnected = false;
                }
            }
        }
        
        // 如果所有客户端都断开连接，则关闭系统
        if (allClientsDisconnected && !clients.empty()) {
            Logger::log(LogLevel::INFO, "All clients disconnected, shutting down system...");
            outputClientInfo();
            shutdownSystem();
            break;
        }
    }
}

void DeamonServer::shutdownSystem() {
    // 执行用户注册的回调函数
    bool callbackResult = true;
    
    {   
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (shutdownCallback) {
            try {
                callbackResult = shutdownCallback();
            } catch (const std::exception& e) {
                Logger::log(LogLevel::ERROR, "Exception in shutdown callback: %s", e.what());
                callbackResult = false;
            }
        }
    }
    
    if (callbackResult) {
        Logger::log(LogLevel::INFO, "Shutdown callback executed successfully, proceeding with system shutdown");
        // 执行系统关机命令
        Misc::poweroff();
    } else {
        Logger::log(LogLevel::INFO, "Shutdown callback failed, aborting shutdown");
    }
}

void DeamonServer::outputClientInfo() {
    std::lock_guard<std::mutex> lock(clientsMutex);
    
    Logger::log(LogLevel::INFO, "=== Client Information ===");
    for (const auto& client : clients) {
        std::string stateStr;
        switch (client.second.state) {
            case ClientState::CONNECTED:
                stateStr = "Connected";
                break;
            case ClientState::MISSING:
                stateStr = "Missing";
                break;
            case ClientState::DISCONNECTED:
                stateStr = "Disconnected";
                break;
            default:
                stateStr = "Unknown";
        }
        
        Logger::log(LogLevel::INFO, "Client PID: %d, Socket FD: %d, State: %s, Missing Heartbeats: %d", 
                  client.second.pid, client.first, stateStr.c_str(), client.second.missingHeartbeatCount);
    }
    Logger::log(LogLevel::INFO, "==========================");
}