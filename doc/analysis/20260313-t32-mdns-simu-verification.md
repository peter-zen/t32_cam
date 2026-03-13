# T32 mDNS Simu 验证记录

## 背景与目标

- 目标：验证 `t32_yb` 侧 mDNS 接入后的 Simu 主链路是否可运行。
- 验证范围：`CMD_MOBILE` 路径下的 HTTP、RTSP、mDNS 启动与退出。
- 验证日期：2026-03-13

## 验证环境

- 仓库：`t32_yb`
- 构建目录：`build_sim`
- 验证命令：
  - `cmake -DBUILD_FOR_SIMULATION=ON ..`
  - `make -j$(nproc) htc_main_app`
  - `CONFIG_FILE=<temp-config> ./htc_main_app -m`

说明：

- 本机 `8080` 端口已被其他进程占用，因此验证时使用临时 `config.ini` 覆盖：
  - `CtrlPort=18080`
  - `RtspPort=18554`
- 为支持这类无侵入验证，Simu 侧已支持：
  - `main_app` 仅在未设置时写入默认环境变量
  - `EnvManager::getEnv()` 在内部 map 缺失时回退读取进程环境变量

## 验证结果

### 1. 构建结果

- `build_sim` 重新配置成功
- `htc_main_app` 编译和链接成功
- 当前仍存在大量 `Logger::log` deprecation warning，但不影响本次功能验证

### 2. 运行结果

Simu 运行 `CMD_MOBILE` 后，日志确认以下链路成立：

1. 选择本机可用网卡，例如 `enp2s0`
2. 获取本机 IPv4，例如 `192.168.0.210`
3. mDNS 成功启动，服务类型为 `_t32cam._tcp.local`
4. HTTP Server 成功启动在 `18080`
5. RTSP Server 成功启动在 `18554`
6. 退出时 HTTP、RTSP、mDNS 均按预期停止

### 3. 端口旁证

运行期间通过 `ss` 观察到：

- `18080/tcp` 被 `htc_main_app` 监听
- `18554/tcp` 被 `htc_main_app` 监听
- `5353/udp` 被 `htc_main_app` 占用

这说明 Simu 下 mDNS socket 已建立，HTTP/RTSP/mDNS 三者已进入同时在线状态。

## 结论

- 当前实现已经满足 V1 的 Simu 主链路目标。
- `CMD_MOBILE` 在 `BUILD_FOR_SIMULATION` 下不会再触发真实 WiFi/DHCP。
- `sn=PID`、`mac=网卡 MAC`、`status=ready` 的字段策略已在运行参数中落地。
- 第三方库与自有封装目录落点符合既定方案。

## 尚未完成的验证

- 本机未安装 `avahi-browse`、`dns-sd` 等 Bonjour 浏览工具，暂未做“外部发现端”验证。
- 尚未在真机局域网环境下确认手机端或桌面 Bonjour 工具的实际发现结果。

## 下一步建议

1. 在真机 `CMD_MOBILE` 路径复测 `_t32cam._tcp.local` 是否可被手机端发现。
2. 使用 Bonjour 工具检查 PTR、SRV、TXT、A 记录内容是否与规格一致。
3. 如需便于本地联调，可继续保留 `CONFIG_FILE` 外部覆盖能力。
