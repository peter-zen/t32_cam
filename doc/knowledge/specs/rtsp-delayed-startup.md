# RTSP Server 延迟启动方案

## 文档信息

- **创建日期**: 2026-01-16
- **作者**: zengping
- **版本**: 1.0
- **状态**: 已实施

---

## 1. 问题背景

### 1.1 现状分析

在当前的 RTSP 服务器实现中，当调用 `RtspServer::start()` 启动服务器时，会立即创建并启动 `MediaSession`，这会导致：

1. **立即打开媒体源**：
   - 真机环境：打开摄像头、音频设备
   - 模拟环境：打开视频文件、音频文件

2. **立即启动生产者线程**：
   - 开始从媒体源拉取数据
   - 将数据推送到 FIFO 队列

3. **客户端未连接时的资源浪费**：
   - FIFO 队列满后，新帧会被丢弃
   - 生产者线程持续运行，消耗 CPU
   - 文件或设备句柄一直占用

### 1.2 当前执行流程

```
RTSP Server 启动 (start())
  ↓
创建 videoSource/audioSourceMedia
  ↓
创建 MediaSession (videoSession/audioSession)
  ↓
调用 MediaSession::start()
  ├─ source->open()      // 打开文件或设备
  └─ 启动 producerThread  // 开始生产数据
  ↓
生产者线程持续往 FIFO push 数据
  ├─ 无客户端连接时 → FIFO 满 → 数据丢弃
  └─ 日志显示大量 "FIFO full, dropping frame"
  ↓
客户端连接 → pullFrame() 首次被调用
  ↓
客户端断开 → onSessionClosed()
  ├─ 调用 source->reset()  // 重置到文件开头
  └─ 生产者线程继续运行（未停止）
```

### 1.3 问题总结

| 问题 | 影响 |
|------|------|
| **资源浪费** | 无客户端时仍占用文件/设备句柄 |
| **CPU 消耗** | 生产者线程持续运行，无客户端也产生数据 |
| **内存占用** | FIFO 被不必要地填充 |
| **日志干扰** | 大量 "FIFO full" 警告 |
| **不符合嵌入式场景** | 真机期望客户端连接后才激活摄像头 |

---

## 2. 设计目标

### 2.1 真机场景需求

对于嵌入式相机，期望的行为：

1. **服务器启动**：监听 RTSP 端口，等待客户端
2. **客户端连接**（PLAY 命令）：激活摄像头和麦克风
3. **数据传输**：实时获取并推流音视频数据
4. **客户端断开**（TEARDOWN 命令）：关闭摄像头和麦克风，释放资源

### 2.2 模拟场景需求

对于 PC 模拟环境，期望的行为：

1. **服务器启动**：监听 RTSP 端口，准备文件源
2. **客户端连接**：打开视频/音频文件
3. **数据传输**：读取文件并推流
4. **客户端断开**：关闭文件，释放资源

### 2.3 统一设计原则

- **按需启动**：客户端连接后才启动媒体会话
- **及时释放**：客户端断开后立即释放资源
- **资源节约**：无客户端时不占用任何资源
- **日志清晰**：只在有实际数据流时记录

---

## 3. 技术方案

### 3.1 方案概述

采用 **延迟启动（Lazy Initialization）** 方案：

```
RTSP Server 启动 (start())
  ↓
创建 videoSource/audioSourceMedia（但不打开）
  ↓
创建 MediaSession 对象（但不启动）
  ↓
注册回调函数
  ↓
启动 RTSP 服务器（监听端口）
  ↓
【等待客户端连接】
  ↓
客户端 PLAY → pullFrame() 首次被调用
  ↓
检测到首次连接 → 启动 MediaSession
  ├─ session->start()
  │  ├─ source->open()      // 打开文件或设备
  │  └─ 启动 producerThread  // 开始生产数据
  └─ 开始往 FIFO push 数据
  ↓
【数据传输中】
  ↓
客户端 TEARDOWN → onSessionClosed() 被调用
  ↓
停止并清理 MediaSession
  ├─ session->stop()
  │  ├─ 停止 producerThread
  │  └─ source->close()     // 关闭文件或设备
  └─ session.reset()        // 释放 MediaSession 对象
  ↓
【等待下一次客户端连接】
```

### 3.2 关键设计点

#### 3.2.1 延迟检测

通过 `pullFrame()` 的首次调用来检测客户端连接：

```cpp
int RtspServer::pullFrame(void **data, size_t *size, uint64_t *timestamp)
{
    auto instance = RtspServer::getInstance();

    // 首次调用时启动 session
    if (!instance->clientConnected.load()) {
        instance->clientConnected.store(true);
        elog_i("RTSP", "Client connected, starting media sessions");

        // 创建并启动 session
        if (!instance->videoSession) {
            instance->videoSession = std::make_shared<MediaSession>(...);
            instance->videoSession->start();
        }
        // ... audio 同理
    }

    // 正常拉取数据
    return MediaSession::pullFrame(data, size, timestamp, ...);
}
```

#### 3.2.2 资源释放

在 `onSessionClosed()` 时完全释放资源：

```cpp
int RtspServer::onSessionClosed(void **data, size_t *size, uint64_t *timestamp)
{
    auto instance = RtspServer::getInstance();

    // 停止并清理 session
    if (instance->videoSession) {
        instance->videoSession->stop();  // 停止线程，关闭源
        instance->videoSession.reset(); // 释放对象
    }

    // 重置源状态
    if (instance->videoSource) {
        instance->videoSource->reset();
    }

    clientConnected.store(false);

    return 0;
}
```

#### 3.2.3 状态管理

使用 `std::atomic<bool> clientConnected` 标志：

- `false`：无客户端连接，session 未启动
- `true`：有客户端连接，session 已启动

### 3.3 代码改动清单

| 文件 | 改动内容 |
|------|---------|
| `src/media/rtsp/RtspServer.cpp::start()` | 移除立即启动 session 的代码 |
| `src/media/rtsp/RtspServer.cpp::pullFrame()` | 添加延迟启动逻辑 |
| `src/media/rtsp/RtspServer.cpp::onSessionClosed()` | 添加停止和清理 session 的逻辑 |
| `src/media/rtsp/RtspServer.cpp::stop()` | 确保停止时清理 session |

---

## 4. 实施细节

### 4.1 RtspServer::start() 改动

**位置**: `src/media/rtsp/RtspServer.cpp` 第 430-444 行

**移除代码**:
```cpp
// 移除以下代码
if (videoSource) {
    videoSession = std::make_shared<MediaSession>(videoSource, 20);
    if (!videoSession->start()) {
        elog_e("RTSP", "Failed to start video session");
        return false;
    }
}

if (audioSourceMedia) {
    audioSession = std::make_shared<MediaSession>(audioSourceMedia, 40);
    if (!audioSession->start()) {
        elog_e("RTSP", "Failed to start audio session");
        return false;
    }
}
```

**添加日志**:
```cpp
elog_i("RTSP", "RTSP server started, waiting for client connection");
```

### 4.2 RtspServer::pullFrame() 改动

**位置**: `src/media/rtsp/RtspServer.cpp` 第 485-512 行

**在函数开头添加延迟启动逻辑**:
```cpp
int RtspServer::pullFrame(void **data, size_t *size, uint64_t *timestamp)
{
    auto instance = RtspServer::getInstance();

    // 首次调用时启动 session（延迟启动）
    if (!instance->clientConnected.load()) {
        instance->clientConnected.store(true);
        elog_i("RTSP", "Client connected, starting media sessions");

        // 启动 video session
        if (instance->videoSource && !instance->videoSession) {
            instance->videoSession = std::make_shared<MediaSession>(instance->videoSource, 20);
            if (!instance->videoSession->start()) {
                elog_e("RTSP", "Failed to start video session");
                return -1;
            }
            elog_i("RTSP", "Video session started");
        }

        // 启动 audio session
        if (instance->audioSourceMedia && !instance->audioSession) {
            instance->audioSession = std::make_shared<MediaSession>(instance->audioSourceMedia, 40);
            if (!instance->audioSession->start()) {
                elog_e("RTSP", "Failed to start audio session");
                return -1;
            }
            elog_i("RTSP", "Audio session started");
        }
    }

    // ... 原有 IDR 请求逻辑 ...
}
```

### 4.3 RtspServer::onSessionClosed() 改动

**位置**: `src/media/rtsp/RtspServer.cpp` 第 452-483 行

**在函数开头添加停止和清理逻辑**:
```cpp
int RtspServer::onSessionClosed(void **data, size_t *size, uint64_t *timestamp)
{
    (void)timestamp;
    elog_i("RTSP", "Client disconnected, stopping media sessions");

    auto instance = RtspServer::getInstance();

    // 停止并清理 video session
    if (instance->videoSession) {
        instance->videoSession->stop();
        instance->videoSession.reset();
        elog_i("RTSP", "Video session stopped and cleaned up");
    }

    // 停止并清理 audio session
    if (instance->audioSession) {
        instance->audioSession->stop();
        instance->audioSession.reset();
        elog_i("RTSP", "Audio session stopped and cleaned up");
    }

    // 重置 source 状态
    if (instance->videoSource) {
        instance->videoSource->reset();
    }
    if (instance->audioSourceMedia) {
        instance->audioSourceMedia->reset();
    }

    g_needRequestIdr = true;
    clientConnected.store(false);

    // ... 调用应用层回调 ...
}
```

### 4.4 RtspServer::stop() 改动

**位置**: `src/media/rtsp/RtspServer.cpp` 第 277-295 行

**在停止服务器前确保 session 被清理**:
```cpp
bool RtspServer::stop()
{
    if (!initialized) {
        return true;
    }

    elog_i("RTSP", "Stopping RTSP server");

    // 停止并清理所有 session
    if (videoSession) {
        videoSession->stop();
        videoSession.reset();
        elog_i("RTSP", "Video session stopped");
    }

    if (audioSession) {
        audioSession->stop();
        audioSession.reset();
        elog_i("RTSP", "Audio session stopped");
    }

    // 停止 RTSP 服务器
    stop_server(this->rtsp_server);

    // ... 其他清理代码 ...
}
```

---

## 5. 预期效果

### 5.1 启动时（无客户端）

```
I/RTSP Starting RTSP server in file source mode
I/RTSP Creating Video File Source: sim_sdcard/video/full_frame_camera.h264
I/RTSP Calling make_shared<VideoFileSource>
I/RTSP RTSP server started, waiting for client connection
```

- ✅ 不打开文件
- ✅ 不启动生产者线程
- ✅ 没有 "FIFO full" 警告
- ✅ 低 CPU 占用

### 5.2 客户端连接时

```
I/RTSP Client connected, starting media sessions
I/VID_FILE VideoFileSource::open() called
I/VID_FILE Opening file: sim_sdcard/video/full_frame_camera.h264
I/VID_FILE File size: 35390748 bytes
I/VID_FILE Opened video file: sim_sdcard/video/full_frame_camera.h264, size: 35390748 bytes
I/MED_SESSION MediaSession started. FIFO size: 20
I/RTSP Video session started
I/AUD_FILE Opened audio file: sim_sdcard/video/full_frame_camera.pcm, size: 3829622 bytes
I/MED_SESSION MediaSession started. FIFO size: 40
I/RTSP Audio session started
```

- ✅ 打开文件/设备
- ✅ 启动生产者线程
- ✅ 开始数据传输

### 5.3 客户端断开时

```
I/RTSP Client disconnected, stopping media sessions
I/MED_SESSION MediaSession stopped. Frames produced: 1234
I/RTSP Video session stopped and cleaned up
I/MED_SESSION MediaSession stopped. Frames produced: 5678
I/RTSP Audio session stopped and cleaned up
```

- ✅ 停止生产者线程
- ✅ 关闭文件/设备
- ✅ 释放 FIFO 和所有资源

### 5.4 资源对比

| 阶段 | 当前方案 | 延迟启动方案 | 改善 |
|------|---------|-------------|------|
| **启动时（无客户端）** | 文件打开、线程运行、FIFO 填充 | 文件未打开、线程未启动、FIFO 空 | ⬇️ CPU -80%, 内存 -90% |
| **客户端连接** | 直接传输 | 延迟 ~10ms（启动开销） | 可忽略 |
| **客户端断开** | 继续运行 | 立即停止释放 | ⬇️ CPU -100%, 内存 -100% |
| **日志** | 大量 "FIFO full" 警告 | 只在有数据流时记录 | 🎯 清晰 |

---

## 6. 风险与注意事项

### 6.1 潜在风险

| 风险 | 影响 | 缓解措施 |
|------|------|---------|
| **首次延迟** | 客户端连接后可能有几十毫秒延迟 | 只在首次连接时延迟，可接受 |
| **多客户端** | 当前不支持多客户端 | 检测到已有连接时返回错误 |
| **竞态条件** | 多线程访问 session | 使用 `clientConnected` 原子标志保护 |

### 6.2 注意事项

1. **多客户端支持**：
   - 当前设计只支持单个 RTSP 客户端
   - 如果需要支持多客户端，需要修改为多 session 管理

2. **真机场景**：
   - 嵌入式环境打开摄像头可能有较长延迟
   - 建议在 PLAY 之前完成设备初始化

3. **状态一致性**：
   - 确保 `clientConnected` 标志与实际 session 状态一致
   - 使用原子操作避免竞态条件

---

## 7. 测试验证

### 7.1 测试用例

| 用例 | 步骤 | 预期结果 |
|------|------|---------|
| **启动测试** | 启动服务器，不连接客户端 | 文件未打开，无生产者线程 |
| **连接测试** | 用 ffplay 连接 | 文件打开，开始传输 |
| **断开测试** | 关闭 ffplay | 文件关闭，线程停止 |
| **重连测试** | 断开后重新连接 | 重新打开文件，开始传输 |
| **停止测试** | 服务器运行中调用 stop() | 所有 session 被清理 |

### 7.2 验证命令

```bash
# 启动服务器
./build_sim/bin/htc_main_app -rs

# 在另一个终端播放
ffplay rtsp://localhost:8554/live

# 观察日志
tail -f sim_sdcard/log/app.log
```

---

## 8. 后续优化建议

### 8.1 多客户端支持

如果需要支持多个 RTSP 客户端同时连接：

1. 使用 `std::map<session_id, MediaSession>` 管理多个 session
2. 在 `onSessionClosed()` 中清理对应的 session
3. 在 `pullFrame()` 中根据 session ID 选择对应的 session

### 8.2 预加载优化

如果首次延迟不可接受：

1. 在服务器启动时打开文件但不读取
2. 在客户端连接时开始读取数据
3. 这样可以减少首次连接延迟

### 8.3 资源池化

如果频繁连接/断开：

1. 使用对象池管理 MediaSession
2. 避免频繁创建/销毁对象
3. 提高性能

---

## 9. 变更记录

| 日期 | 版本 | 变更内容 | 作者 |
|------|------|---------|------|
| 2026-01-16 | 1.0 | 初始版本，创建文档 | zengping |
| | | 实施延迟启动方案 | zengping |

---

## 10. 参考资料

- [RTSP 协议规范](https://tools.ietf.org/html/rfc2326)
- [SmolRTSP 文档](https://github.com/dabbertorres/smolrtsp)
- [MediaFIFO 设计文档](../fifo/MediaFIFO_DESIGN.md)
- [MediaSession 架构文档](../rtsp/MEDIA_SESSION_ARCHITECTURE.md)

---

**文档结束**
