# mDNS SIMU 与 Bonjour 验证操作手册

## 1. 目的

给后续会话提供一条最小充分、可重复的 mDNS 验证路径，用于：
- 在 Simu 下启动 `CMD_MOBILE` 主链路
- 验证 HTTP / RTSP / mDNS 是否同时上线
- 使用本机或外部 Bonjour 工具验证发现结果
- 在失败时按固定顺序排查

这篇文档是操作手册，不是协议规格。规格与分层原因请先看：
- `../specs/mdns-device-discovery-behavior.md`
- `../decisions/mdns-cmd-mobile-lifecycle-model.md`
- `../refs/mdns-code-entry-and-config-keys.md`

## 2. 适用范围

适用于：
- `BUILD_FOR_SIMULATION=ON` 的 PC 仿真验证
- 验证 `CMD_MOBILE` 下 mDNS / HTTP / RTSP 主链路
- 使用 `ss`、Bonjour 浏览工具或系统发现工具做旁证

不适用于：
- 证明 Android / iOS 实机发现闭环已经完全成立
- 证明真机 Wi‑Fi 环境下的所有网络变化场景都稳定
- 证明 TXT / PTR / SRV / A 记录都已百分之百符合最终产品态

## 3. 当前推荐验证入口

### 3.1 运行入口
当前 mDNS 主入口不在独立测试程序，而是在：
- `htc_main_app -m`

原因：
- mDNS 当前绑定 `CMD_MOBILE`
- 同一路径下还会启动 HTTP server 与 RTSP server
- 这是最接近真实产品编排的入口

### 3.2 推荐旁证工具
优先使用：
- `ss`：检查 `5353/udp`、HTTP、RTSP 端口监听
- Bonjour 浏览工具：
  - `avahi-browse`
  - `dns-sd`

如果系统没装这些工具，也至少先做端口与日志层验证。

## 4. 前置条件

### 4.1 已完成仿真构建
在仓库根目录执行：
```bash
mkdir -p build_sim && cd build_sim
cmake -DBUILD_FOR_SIMULATION=ON ..
make -j$(nproc) htc_main_app
```

### 4.2 端口规划
若本机默认端口已占用，建议通过外部配置覆盖：
- `CtrlPort=18080`
- `RtspPort=18554`

这不是锦上添花，而是避免本机已有服务干扰验证。

### 4.3 准备临时配置文件
建议基于现有配置复制一份临时文件，只改 `[MDNS]` 相关项，例如：
```ini
[MDNS]
Enable=1
ServiceType=_t32cam._tcp
InstanceName=T32Camera-Simu
HostName=t32cam-simu
CtrlPort=18080
RtspPort=18554
```

如果你不明确指定配置文件来源，就不要假设仓库默认配置一定适合本机当前端口环境。

## 5. 标准验证流程

### 5.1 启动主程序
在 `build_sim` 目录执行：
```bash
CONFIG_FILE=/path/to/temp-config.ini ./htc_main_app -m
```

### 5.2 观察日志关键点
预期应看到同类信息：
- 已选定可用网卡
- 已拿到 IPv4 地址
- mDNS 启动成功
- HTTP server 启动成功
- RTSP server 启动成功

如果连“获取 IP”都失败，后面不要继续看 Bonjour；问题还在前面。

### 5.3 检查监听端口
在另一个终端执行：
```bash
ss -luntp | grep -E '5353|18080|18554'
```

预期至少应看到：
- `5353/udp`
- `18080/tcp`（或你的 `CtrlPort`）
- `18554/tcp`（或你的 `RtspPort`）

### 5.4 检查 HTTP 连通性
```bash
curl http://127.0.0.1:18080/api/health
curl http://127.0.0.1:18080/healthz
```

### 5.5 检查 RTSP 可达性
可按项目既有 RTSP 验证方式进一步确认，例如：
```bash
ffplay rtsp://127.0.0.1:18554/live
```

这里是否有画面取决于当前流源与环境，不要把 RTSP 媒体问题和 mDNS 发布问题混为一谈。

## 6. Bonjour / mDNS 浏览验证

### 6.1 使用 `avahi-browse`
如果系统安装了 Avahi：
```bash
avahi-browse -rt _t32cam._tcp
```

重点看：
- 是否能发现 `_t32cam._tcp` 服务
- 实例名是否符合预期
- 主机名 / 地址 / 端口是否符合配置

### 6.2 使用 `dns-sd`
如果系统安装了 Bonjour 命令行：
```bash
dns-sd -B _t32cam._tcp
dns-sd -L <instance-name> _t32cam._tcp local
```

重点看：
- Browse 能否发现实例
- Lookup 时能否看到 hostname、port、TXT

### 6.3 当前验证边界
即便 browse 成功，也只能证明：
- 当前局域网/本机环境下服务已被发布并可发现

不能直接证明：
- Android / iOS UI 侧发现流程完全无误
- 所有 TXT 字段都符合产品最终约束
- 真机网卡切换和 DHCP 变化已完全处理

## 7. 推荐排查顺序

### 7.1 `htc_main_app -m` 启动即失败
先查：
- 配置文件路径是否正确
- `Enable=1` 是否启用 mDNS
- 是否拿到了可用网卡和 IPv4
- 端口是否已被占用

### 7.2 HTTP/RTSP 正常，但没有 `5353/udp`
优先怀疑：
- mDNS 没启用
- `MdnsService::start(...)` 失败
- 配置的 IP 非法或获取失败

### 7.3 `5353/udp` 在，但浏览工具发现不到
优先查：
- 使用的服务类型是不是 `_t32cam._tcp`
- 是否需要带 `local`
- 本机/局域网是否允许组播
- 工具是否真的已安装且工作正常

### 7.4 browse 能发现，但端口或 TXT 不对
优先回头查：
- `[MDNS]` 配置项
- `buildMdnsParams(...)`
- `MdnsTxtRecord::build(...)`
- model 归一化逻辑是否触发 fallback

### 7.5 status 行为与预期不一致
当前要先记住一个事实：
- `updateStatus()` 不是在线原位修改
- 当前模型是 stop + restart 重注册

如果你按“在线热更新 TXT”来理解日志，那排查方向一开始就错了。

## 8. 常见错误理解

- 不要把 mDNS 验证和 HTTP API 验证完全割裂；当前它们同属 `CMD_MOBILE` 主链路
- 不要把 `5353/udp` 已监听直接等同于手机端一定能发现
- 不要把 browse 成功直接等同于真机闭环已完成
- 不要忽略 `[MDNS]` 里的 `CtrlPort` / `RtspPort` 对真实服务端口的影响
- 不要把 Simu 的发现成功机械外推为真机 Wi‑Fi 条件下也完全等价

## 9. 当前推荐阅读顺序

进入 mDNS 主题时建议按下面顺序：
1. `../specs/mdns-device-discovery-behavior.md`
2. `../decisions/mdns-cmd-mobile-lifecycle-model.md`
3. `../refs/mdns-code-entry-and-config-keys.md`
4. 本文
5. 再看：
   - `src/app/main_app.cpp`
   - `src/service/discovery/MdnsService.cpp`
   - `tests/test_mdns_txt_record.cpp`
   - `tests/test_mdns_model_normalization.cpp`
