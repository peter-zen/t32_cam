# mDNS 代码入口与配置键参考

## 1. 目的

给后续会话提供最小充分入口，用于快速回答：
- mDNS 当前从哪里启动
- discovery 代码具体在哪
- TXT Record 从哪里生成
- 构建接线在哪
- 该先查哪些配置键

## 2. 当前最重要代码入口

### 2.1 主编排入口
- `src/app/main_app.cpp`

优先关注：
- `buildMdnsParams(...)`
- `isMdnsEnabled(...)`
- `CMD_MOBILE` 分支中的 mDNS / HTTP / RTSP 启停顺序

### 2.2 discovery 服务封装
- `src/service/discovery/MdnsService.h`
- `src/service/discovery/MdnsService.cpp`

这里负责：
- 参数归一化
- IPv4 校验
- 调用 `mdnsd_start()`
- 设置 hostname
- 注册服务
- 停止与重注册

### 2.3 TXT Record 生成
- `src/service/discovery/MdnsTxtRecord.h`
- `src/service/discovery/MdnsTxtRecord.cpp`

### 2.4 构建接线
- `src/service/discovery/CMakeLists.txt`
- `src/service/CMakeLists.txt`
- `third_party/CMakeLists.txt`
- `third_party/tinysvcmdns/CMakeLists.txt`

### 2.5 测试入口
- `tests/test_mdns_txt_record.cpp`
- `tests/test_mdns_model_normalization.cpp`

## 3. 当前关键函数

### 3.1 `buildMdnsParams(...)`
位于：
- `src/app/main_app.cpp`

作用：
- 从配置与运行态网卡/IP/端口构造 `MdnsServiceParams`

### 3.2 `isMdnsEnabled(...)`
位于：
- `src/app/main_app.cpp`

作用：
- 判断 `[MDNS]` 配置是否启用 mDNS

### 3.3 `MdnsService::start(...)`
位于：
- `src/service/discovery/MdnsService.cpp`

作用：
- 外部统一入口
- 当前实现为 stop 后再 start

### 3.4 `MdnsService::updateStatus(...)`
位于：
- `src/service/discovery/MdnsService.cpp`

作用：
- 更新 `txt.status`
- 当前运行态下采用 stop + restart 重注册

### 3.5 `MdnsTxtRecord::build(...)`
位于：
- `src/service/discovery/MdnsTxtRecord.cpp`

作用：
- 统一拼装 TXT Record KV 列表

## 4. 当前关键配置语义

根据 `main_app.cpp` 当前使用方式，重点关注 `[MDNS]` 段下这些键：
- `Enable`
- `ServiceType`
- `InstanceName`
- `HostName`
- `CtrlPort`
- `RtspPort`

其中要特别注意：
- `CtrlPort` 不只是 mDNS 文本字段，还直接决定 HTTP server 监听端口
- `RtspPort` 不只是 mDNS 文本字段，还直接决定 RTSP server 监听端口

如果你把它们理解成“只影响广播，不影响实际服务端口”，那就是错误理解。

## 5. 当前默认值与归一化要点

### 5.1 服务类型
默认输入：
- `_t32cam._tcp`

归一化后：
- 自动补 `.local`
- 空值回退 `_t32cam._tcp.local`

### 5.2 主机名
当前会：
- 去引号
- 去空白
- 空格转 `-`
- 非法字符转 `-`
- 转小写
- 自动补 `.local`

### 5.3 model
当前会：
- 空值回退 `T32`
- `CXXX` 回退 `T32`

## 6. 默认阅读顺序

进入 mDNS 主题时，建议默认按下面顺序：
1. `doc/knowledge/specs/mdns-device-discovery-behavior.md`
2. `doc/knowledge/decisions/mdns-cmd-mobile-lifecycle-model.md`
3. 本文
4. 再去读：
   - `src/app/main_app.cpp`
   - `src/service/discovery/MdnsService.cpp`
   - `src/service/discovery/MdnsTxtRecord.cpp`
   - 对应测试文件

## 7. 当前不应再重复的错误理解

- 不要把 mDNS 当成独立 daemon
- 不要把 discovery 放进 `src/network/` 语义里理解
- 不要把 `[MDNS]` 里的端口当成“只给广播看的元数据”
- 不要把 `updateStatus()` 理解成无重启的在线原位更新
- 不要把方案文档里的目标态写成当前已验证事实
