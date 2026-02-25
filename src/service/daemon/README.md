# 进程守护模块

## 简介
进程守护模块是一个用于管理多个用户进程并确保系统稳定运行的组件。它包含一个守护服务器和多个用户客户端，通过心跳机制监控用户进程的状态。当所有用户进程断开连接或心跳停止一段时间后，守护服务器会自动关闭系统。

## 接口说明
所有公共接口都在`daemon_api.h`文件中定义，用户只需包含该文件即可使用所有功能。

## 设计架构

### 核心组件
1. **守护服务器（DeamonServer）**：监听客户端连接，管理客户端信息，监控客户端心跳，在必要时执行系统关机
2. **守护客户端（DeamonClient）**：连接到守护服务器，定期发送心跳包，维持与服务器的通信

### 通信协议
- 基于TCP协议，固定端口号65530
- 采用二进制消息格式，包含注册消息、心跳消息和确认消息

## 核心功能

### 1. 握手认证
- 用户客户端连接到守护服务器后，发送包含进程PID和心跳间隔的注册消息
- 守护服务器验证并记录客户端信息，返回确认消息

### 2. 心跳机制
- 客户端按照约定间隔发送心跳包
- 服务器接收并更新客户端状态
- 客户端发送心跳失败时自动重试一次

### 3. 失联检测
- 服务器连续3个心跳间隔未收到心跳包时，将客户端标记为失联
- 服务器监控所有客户端状态，判断系统是否可以关闭

### 4. 自动关机
- 当所有客户端都断开连接或标记为失联时，服务器执行注册的回调函数后关机
- 关机前将所有客户端信息输出到串口

### 5. 事件回调
- 提供注册关机回调函数的接口，允许用户在关机前执行清理操作

## 接口定义

### 公共接口

```cpp
// 注册关机回调函数
bool registerShutdownCallback(std::function<bool()> callback);

// 启动守护服务器
bool startDaemonServer();

// 停止守护服务器
void stopDaemonServer();

// 向守护服务器注册客户端（守护服务器地址内部固定为127.0.0.1）
bool registerToDaemonServer(int pid, int heartbeatIntervalMs);

// 从守护服务器注销客户端
void unregisterFromDaemonServer();

// 检查与守护服务器的连接是否活跃
bool isDaemonConnectionActive();
```

## 实现细节

### 线程安全
- 使用互斥锁保护共享资源的访问
- 守护服务器使用单独的线程处理客户端连接和心跳监控
- 守护客户端使用单独的线程发送心跳包

### 错误处理
- 完善的错误检测和日志记录
- 客户端心跳发送失败时自动重试机制
- 异常情况下的资源清理

## 使用示例

### 守护进程示例

```cpp
#include "daemon_api.h"
#include <iostream>

// 关机回调函数
bool myShutdownCallback() {
    std::cout << "执行关机前的清理操作..." << std::endl;
    // 在这里执行必要的清理工作
    return true; // 返回true表示可以继续关机
}

int main() {
    // 注册关机回调函数
    registerShutdownCallback(myShutdownCallback);
    
    // 启动守护服务器
    if (startDaemonServer()) {
        std::cout << "守护服务器启动成功" << std::endl;
        // 保持程序运行
        while (true) {
            sleep(1);
        }
    } else {
        std::cerr << "守护服务器启动失败" << std::endl;
        return 1;
    }
    
    return 0;
}
```

### 用户进程示例

```cpp
#include "daemon_api.h"
#include <iostream>
#include <unistd.h>

int main() {
    int intervalMs = 3000; // 3秒发送一次心跳
    
    // 注册到守护服务器
    if (registerToDaemonServer("127.0.0.1", intervalMs)) {
        std::cout << "成功注册到守护服务器，PID: " << getpid() << std::endl;
        
        // 执行用户进程的主要逻辑
        // ...
        
        // 正常退出前注销
        unregisterFromDaemonServer();
    } else {
        std::cerr << "注册到守护服务器失败" << std::endl;
        return 1;
    }
    
    return 0;
}
```

### 编译和链接

守护模块编译时需要链接pthread库，在CMakeLists.txt中已经配置了相关依赖。使用示例代码时，请确保包含daemon目录并链接相应的库。

## 注意事项

1. 守护服务器应该作为系统的守护进程运行，确保系统启动时自动启动
2. 用户进程应在启动时立即注册到守护服务器，并在退出前正确注销
3. 心跳间隔不宜过短，建议设置为2-5秒，避免网络拥塞
4. 关机回调函数应尽量简洁，避免执行耗时操作
5. 所有进程应妥善处理信号，确保在收到终止信号时能正确清理资源并退出