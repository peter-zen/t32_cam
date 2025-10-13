// daemon_example.cpp
// 进程守护模块使用示例

#include "daemon_api.h"
#include "Logger.h"
#include <unistd.h>
#include <signal.h>
#include <atomic>

std::atomic<bool> running(true);

// 信号处理函数
void signalHandler(int signum) {
    Logger::log(LogLevel::INFO, "Received signal %d, shutting down...", signum);
    running = false;
}

// 守护服务器关机回调函数
bool shutdownCallback() {
    Logger::log(LogLevel::INFO, "Executing shutdown callback...");
    // 在这里可以执行一些清理工作
    return true;
}

// 守护服务器示例
void runDaemonServerExample() {
    Logger::log(LogLevel::INFO, "=== Starting Daemon Server Example ===");
    
    // 注册关机回调函数
    if (!registerShutdownCallback(shutdownCallback)) {
        Logger::log(LogLevel::ERROR, "Failed to register shutdown callback");
        return;
    }
    
    // 启动守护服务器
    if (!startDaemonServer()) {
        Logger::log(LogLevel::ERROR, "Failed to start daemon server");
        return;
    }
    
    Logger::log(LogLevel::INFO, "Daemon server started, waiting for clients...");
    
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 主循环
    while (running) {
        sleep(1);
    }
    
    // 停止守护服务器
    stopDaemonServer();
    Logger::log(LogLevel::INFO, "Daemon server stopped");
}

// 守护客户端示例
void runDaemonClientExample() {
    Logger::log(LogLevel::INFO, "=== Starting Daemon Client Example ===");
    
    // 获取当前进程ID
    int pid = getpid();
    // 设置心跳间隔为2000毫秒
    int intervalMs = 2000;
    
    // 注册到守护服务器
    if (!registerToDaemonServer(pid, intervalMs)) {
        Logger::log(LogLevel::ERROR, "Failed to register to daemon server");
        return;
    }
    
    Logger::log(LogLevel::INFO, "Registered to daemon server with PID=%d, interval=%dms", pid, intervalMs);
    
    // 注册信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // 主循环
    int counter = 0;
    while (running) {
        sleep(1);
        counter++;
        
        // 每5秒检查一次连接状态
        if (counter % 5 == 0) {
            if (isDaemonConnectionActive()) {
                Logger::log(LogLevel::INFO, "Connection to daemon server is active");
            } else {
                Logger::log(LogLevel::ERROR, "Connection to daemon server is not active");
            }
        }
        
        // 运行15秒后退出
        if (counter >= 15) {
            Logger::log(LogLevel::INFO, "Client example finished after 15 seconds");
            break;
        }
    }
    
    // 从守护服务器注销
    unregisterFromDaemonServer();
    Logger::log(LogLevel::INFO, "Unregistered from daemon server");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        Logger::log(LogLevel::ERROR, "Usage: %s [server|client]", argv[0]);
        return 1;
    }
    
    std::string mode = argv[1];
    
    if (mode == "server") {
        runDaemonServerExample();
    } else if (mode == "client") {
        runDaemonClientExample();
    } else {
        Logger::log(LogLevel::ERROR, "Invalid mode. Use 'server' or 'client'");
        return 1;
    }
    
    return 0;
}