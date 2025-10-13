// daemon.h
// 进程守护模块公共头文件，定义共享常量和结构体

#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

// 服务器监听端口
constexpr uint16_t DAEMON_SERVER_PORT = 65530;

// 心跳包最大重试次数
constexpr int HEARTBEAT_MAX_RETRY = 1;

// 连续丢失心跳包的最大次数，超过此次数客户端被标记为失联
constexpr int MAX_MISSING_HEARTBEATS = 3;

// 消息类型枚举
enum class MessageType : uint8_t {
    REGISTER = 0,     // 客户端注册
    HEARTBEAT = 1,    // 心跳包
    UNREGISTER = 2,   // 客户端注销
    ACK = 3           // 服务器确认
};

// 客户端状态枚举
enum class ClientState : uint8_t {
    CONNECTED = 0,    // 已连接
    MISSING = 1,      // 失联
    DISCONNECTED = 2  // 已断开连接
};

// 注册消息结构体
struct RegisterMessage {
    MessageType type;     // 消息类型，应为REGISTER
    pid_t pid;            // 客户端进程PID
    int32_t intervalMs;   // 心跳间隔（毫秒）
    
    // 序列化方法
    std::string serialize() const {
        std::string data(sizeof(RegisterMessage), 0);
        data[0] = static_cast<uint8_t>(type);
        memcpy(const_cast<char*>(data.data()) + 1, &pid, sizeof(pid_t));
        memcpy(const_cast<char*>(data.data()) + 1 + sizeof(pid_t), &intervalMs, sizeof(int32_t));
        return data;
    }
    
    // 反序列化方法
    bool deserialize(const std::string& data) {
        if (data.size() != sizeof(RegisterMessage)) {
            return false;
        }
        type = static_cast<MessageType>(data[0]);
        memcpy(&pid, data.data() + 1, sizeof(pid_t));
        memcpy(&intervalMs, data.data() + 1 + sizeof(pid_t), sizeof(int32_t));
        return type == MessageType::REGISTER;
    }
};

// 心跳消息结构体
struct HeartbeatMessage {
    MessageType type;  // 消息类型，应为HEARTBEAT
    pid_t pid;         // 客户端进程PID
    
    // 序列化方法
    std::string serialize() const {
        std::string data(sizeof(HeartbeatMessage), 0);
        data[0] = static_cast<uint8_t>(type);
        memcpy(const_cast<char*>(data.data()) + 1, &pid, sizeof(pid_t));
        return data;
    }
    
    // 反序列化方法
    bool deserialize(const std::string& data) {
        if (data.size() != sizeof(HeartbeatMessage)) {
            return false;
        }
        type = static_cast<MessageType>(data[0]);
        memcpy(&pid, data.data() + 1, sizeof(pid_t));
        return type == MessageType::HEARTBEAT;
    }
};

// 确认消息结构体
struct AckMessage {
    MessageType type;  // 消息类型，应为ACK
    bool success;      // 操作是否成功
    
    // 序列化方法
    std::string serialize() const {
        std::string data(sizeof(AckMessage), 0);
        char* buffer = const_cast<char*>(data.data());
        buffer[0] = static_cast<uint8_t>(type);
        buffer[1] = success ? 1 : 0;
        return data;
    }
    
    // 反序列化方法
    bool deserialize(const std::string& data) {
        if (data.size() != sizeof(AckMessage)) {
            return false;
        }
        type = static_cast<MessageType>(data[0]);
        success = (data[1] != 0);
        return type == MessageType::ACK;
    }
};