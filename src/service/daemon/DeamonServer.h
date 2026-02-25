// DeamonServer.h
// 守护服务器内部实现头文件
// 此文件包含守护服务器的内部实现细节，对用户隐藏

#pragma once

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include "daemon.h"

// 前向声明
class DeamonServerImpl;

// 客户端信息结构体
struct ClientInfo {
    int socketFd;                  // 客户端socket文件描述符
    pid_t pid;                     // 客户端进程PID
    int heartbeatIntervalMs;       // 心跳间隔（毫秒）
    int missingHeartbeatCount;     // 丢失的心跳包计数
    ClientState state;             // 客户端状态
    std::chrono::steady_clock::time_point lastHeartbeatTime; // 最后一次心跳时间
};

/**
 * @brief 守护服务器类
 */
class DeamonServer {
public:
    /**
     * @brief 获取守护服务器单例
     * @return 守护服务器实例
     */
    static DeamonServer& getInstance();
    
    /**
     * @brief 启动服务器
     * @return 启动是否成功
     */
    bool start();
    
    /**
     * @brief 停止服务器
     */
    void stop();
    
    /**
     * @brief 注册关机回调函数
     * @param callback 回调函数
     * @return 注册是否成功
     */
    bool registerCallback(std::function<bool()> callback);
    
public:
    /**
     * @brief 析构函数
     */
    ~DeamonServer();
    
private:
    // 私有构造函数，防止外部实例化
    DeamonServer();
    
    // 服务器主循环
    void serverMainLoop();
    
    // 客户端连接处理线程函数
    void clientHandlerThread(int clientSocket);
    
    // 心跳监控线程函数
    void heartbeatMonitorThread();
    
    // 发送关闭系统命令
    void shutdownSystem();
    
    // 输出客户端信息到串口
    void outputClientInfo();
    
    // 成员变量
    int serverSocket;              // 服务器socket文件描述符
    bool running;                  // 服务器运行状态
    std::thread serverThread;      // 服务器线程
    std::thread monitorThread;     // 心跳监控线程
    std::mutex clientsMutex;       // 客户端列表互斥锁
    std::unordered_map<int, ClientInfo> clients; // 客户端信息映射表
    std::function<bool()> shutdownCallback; // 关机回调函数
    std::mutex callbackMutex;      // 回调函数互斥锁
    
    // 防止拷贝构造和赋值
    DeamonServer(const DeamonServer&) = delete;
    DeamonServer& operator=(const DeamonServer&) = delete;
};