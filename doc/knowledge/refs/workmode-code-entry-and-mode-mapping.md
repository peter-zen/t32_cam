# WorkMode 代码入口与模式映射参考

## 1. 目的

给后续会话提供最小充分入口，用于快速回答：
- 工作模式当前从哪里读
- 映射到哪些主程序命令路径
- HTTP workmode 请求当前做了什么
- 哪些地方容易被误读

## 2. 当前最重要代码入口

### 2.1 工作模式模块
- `src/app/workmode/WorkMode.h`
- `src/app/workmode/WorkMode.cpp`

### 2.2 主程序命令编排
- `src/app/main_app.cpp`

重点关注：
- `CMD_*` 宏定义
- `working_mode` 到 `command` 的 `switch` 映射
- `CMD_MOBILE` 分支

### 2.3 HTTP 接口入口
- `src/service/http_server/http_api_v1.cpp`

重点关注：
- `api_v1_system_workmode()`

### 2.4 相关构建接线
- `src/app/workmode/CMakeLists.txt`
- `src/app/CMakeLists.txt`

## 3. 当前工作模式枚举

- `WORKING_MODE_SNAP_ONLY`
- `WORKING_MODE_SNAP_UPLOAD`
- `WORKING_MODE_UPLOAD_ONLY`
- `WORKING_MODE_TEST_ONLY`
- `WORKING_MODE_UVC`
- `WORKING_MODE_MAX`

## 4. 当前启动映射

`main_app.cpp` 当前映射为：
- `SNAP_ONLY` -> `CMD_SNAP`
- `UPLOAD_ONLY` -> `CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD`
- `TEST_ONLY` -> `CMD_MOBILE`
- `SNAP_UPLOAD` -> `CMD_SNAP | CMD_CONN_NET | CMD_DHCP | CMD_NTP | CMD_UPLOAD`
- `UVC` -> `CMD_CONN_NET | CMD_DHCP | CMD_RTSP_SERVER`

## 5. 当前 HTTP workmode handler 的真实动作

`api_v1_system_workmode()` 当前只做：
- 校验 POST
- 校验 JSON 中 `mode` 为 int
- 记录日志
- 返回：
  - `mode`
  - `accepted=true`

当前没有看到：
- 修改 `WorkMode`
- 更新 MCU / GPIO
- 切换运行中命令路径
- 广播切换事件

## 6. 当前最容易犯的理解错误

- 不要把 `CMD_MOBILE` 当成 `WorkMode` 枚举值
- 不要把 `/api/v1/system/workmode` 当成已完成真实切换的接口
- 不要把启动期硬件模式选择和运行期远程切换混在一起
- 不要把 `accepted=true` 理解为“切换已完成”

## 7. 推荐阅读顺序

进入 workmode 主题时建议按下面顺序：
1. `doc/knowledge/specs/workmode-selection-and-switching.md`
2. `doc/knowledge/decisions/workmode-vs-cmd-mobile-layering.md`
3. 本文
4. 再看：
   - `src/app/workmode/WorkMode.cpp`
   - `src/app/main_app.cpp`
   - `src/service/http_server/http_api_v1.cpp`
