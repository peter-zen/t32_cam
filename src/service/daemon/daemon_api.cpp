// daemon_api.cpp
// 进程守护模块公共API实现文件

#include "daemon_api.h"
#include "DeamonServer.h"
#include "DeamonClient.h"
#include <memory>
#include <mutex>
#include <unistd.h>

// 全局变量用于单例实现
static std::mutex clientInstanceMutex;
static std::unique_ptr<DeamonClient> clientInstance;

/**
 * 注册系统关机前的回调函数
 * @param callback 回调函数，返回true表示可以继续关机，返回false表示取消关机
 * @return 注册成功返回true，失败返回false
 */
bool registerShutdownCallback(std::function<bool()> callback) {
    return DeamonServer::getInstance().registerCallback(callback);
}

/**
 * 启动守护服务器
 * @return 启动成功返回true，失败返回false
 */
bool startDaemonServer() {
    return DeamonServer::getInstance().start();
}

/**
 * 停止守护服务器
 */
void stopDaemonServer() {
    DeamonServer::getInstance().stop();
}

/**
 * 向守护服务器注册客户端进程
 * @param pid 客户端进程的PID
 * @param heartbeatIntervalMs 心跳间隔（毫秒）
 * @return 注册成功返回true，失败返回false
 */
bool registerToDaemonServer(int pid, int heartbeatIntervalMs) {
    std::lock_guard<std::mutex> lock(clientInstanceMutex);
    if (!clientInstance) {
        clientInstance.reset(new DeamonClient());
    }
    // 使用传入的pid参数
    return clientInstance->registerToServer(pid, heartbeatIntervalMs);
}

/**
 * 从守护服务器注销客户端进程
 * @return 注销成功返回true，失败返回false
 */
bool unregisterFromDaemonServer() {
    std::lock_guard<std::mutex> lock(clientInstanceMutex);
    if (!clientInstance) {
        return false;
    }
    return clientInstance->unregisterFromServer();
}

/**
 * 检查与守护服务器的连接是否活跃
 * @return 连接活跃返回true，不活跃返回false
 */
bool isDaemonConnectionActive() {
    std::lock_guard<std::mutex> lock(clientInstanceMutex);
    if (!clientInstance) {
        return false;
    }
    return clientInstance->isConnectionActive();
}