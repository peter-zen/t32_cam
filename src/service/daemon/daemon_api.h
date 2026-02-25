// daemon_api.h
// 进程守护模块公共API头文件
// 此文件只包含提供给用户使用的公共接口，隐藏内部实现细节

#ifndef DAEMON_API_H
#define DAEMON_API_H

#include <functional>
#include <string>

/**
 * 注册系统关机前的回调函数
 * @param callback 回调函数，返回true表示可以继续关机，返回false表示取消关机
 * @return 注册成功返回true，失败返回false
 */
bool registerShutdownCallback(std::function<bool()> callback);

/**
 * 启动守护服务器
 * @return 启动成功返回true，失败返回false
 */
bool startDaemonServer();

/**
 * 停止守护服务器
 */
void stopDaemonServer();

/**
 * 向守护服务器注册客户端进程
 * @param pid 客户端进程的PID
 * @param heartbeatIntervalMs 心跳间隔（毫秒）
 * @return 注册成功返回true，失败返回false
 * @note 守护服务器地址内部固定为127.0.0.1
 */
bool registerToDaemonServer(int pid, int heartbeatIntervalMs);

/**
 * 从守护服务器注销客户端进程
 * @return 注销成功返回true，失败返回false
 */
bool unregisterFromDaemonServer();

/**
 * 检查与守护服务器的连接是否活跃
 * @return 连接活跃返回true，不活跃返回false
 */
bool isDaemonConnectionActive();

#endif // DAEMON_API_H