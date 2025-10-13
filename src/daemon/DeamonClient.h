// DeamonClient.h
// 守护客户端内部实现头文件
// 此文件包含守护客户端的内部实现细节，对用户隐藏

#ifndef DEAMON_CLIENT_H
#define DEAMON_CLIENT_H

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include "daemon.h"

// 前向声明
class DeamonClientImpl;

/**
 * 守护客户端类
 * 负责与守护服务器进行通信，包括注册和心跳包发送
 */
class DeamonClient {
public:
    DeamonClient();
    ~DeamonClient();
    
    /**
     * 注册到守护服务器
     * @param pid 进程ID
     * @param intervalMs 心跳间隔（毫秒）
     * @return true 成功，false 失败
     */
    bool registerToServer(int pid, int intervalMs);
    
    /**
     * 从守护服务器注销
     * @return true 成功，false 失败
     */
    bool unregisterFromServer();
    
    /**
     * 检查与守护服务器的连接是否活跃
     * @return true 活跃，false 不活跃
     */
    bool isConnectionActive();
    
    /**
     * 停止客户端
     */
    void stop();
    
private:
    std::unique_ptr<DeamonClientImpl> impl;
};

#endif // DEAMON_CLIENT_H