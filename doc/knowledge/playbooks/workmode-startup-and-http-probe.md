# WorkMode 启动判定与 HTTP 探针操作手册

## 1. 目的

给后续会话提供一条最小充分、可重复的 workmode 验证路径，用于：
- 理解启动期 workmode 是如何决定主程序命令路径的
- 验证 `/api/v1/system/workmode` 当前到底只做到什么程度
- 避免把 accepted 响应误当成真实切换完成

这篇文档是操作手册，不是行为规格。请先结合：
- `../specs/workmode-selection-and-switching.md`
- `../decisions/workmode-vs-cmd-mobile-layering.md`
- `../refs/workmode-code-entry-and-mode-mapping.md`

## 2. 适用范围

适用于：
- 理解启动期模式判定
- 人工验证 HTTP workmode handler 当前行为
- 代码阅读与最小行为确认

不适用于：
- 证明运行时真实模式切换已经闭环
- 证明 mobile pairing 完成后一定能退出 `CMD_MOBILE`

## 3. 当前推荐验证思路

当前最稳的做法不是假设“切模式一定已实现”，而是分成两段看：
1. 启动期：`WorkMode` 如何映射到 `command`
2. 运行期：HTTP `/api/v1/system/workmode` 目前只返回什么

## 4. 启动期验证重点

优先读：
- `src/app/workmode/WorkMode.cpp`
- `src/app/main_app.cpp`

重点确认：
- 非 MCU 下是 GPIO 两位判定
- MCU 下是 `readWorkingMode()` 判定
- `WORKING_MODE_TEST_ONLY` 当前映射到 `CMD_MOBILE`

如果你一开始就把 `mode=1/2/3` 直接等同于某条 HTTP runtime 行为路径，验证方向就已经偏了。

## 5. HTTP handler 探针

### 5.1 发送合法请求
例如：
```bash
curl -X POST http://127.0.0.1:8080/api/v1/system/workmode \
  -H 'Content-Type: application/json' \
  -d '{"mode":1}'
```

预期当前返回类似：
```json
{
  "code": 0,
  "message": "success",
  "data": {
    "mode": 1,
    "accepted": true
  }
}
```

### 5.2 发送非法请求
缺少 `mode`：
```bash
curl -X POST http://127.0.0.1:8080/api/v1/system/workmode \
  -H 'Content-Type: application/json' \
  -d '{}'
```

预期：
- HTTP `400`
- 错误原因是缺少 `mode`

### 5.3 方法错误
```bash
curl http://127.0.0.1:8080/api/v1/system/workmode
```

预期：
- HTTP `405`

## 6. 当前验证结论应该怎么写

如果只做了上述探针，你最多只能写：
- handler 存在
- 参数校验存在
- accepted response 存在

你不能写：
- 设备工作模式已真实切换
- mDNS / RTSP / HTTP 生命周期已联动变化
- 运行中主程序命令路径已重编排

## 7. 推荐排查顺序

### 7.1 想确认启动期模式来源
先看 `WorkMode.cpp`，不要先看 HTTP。

### 7.2 想确认运行期是否真切换
先看 `api_v1_system_workmode()` 有没有真正调用后端切换逻辑。
如果没有，就不要幻想日志外还有隐藏闭环。

### 7.3 想确认 `CMD_MOBILE` 和 workmode 的关系
先回到 `main_app.cpp` 的命令映射，不要用产品术语替代代码层概念。

## 8. 常见错误理解

- 不要把 `accepted=true` 当成切换完成
- 不要把 `CMD_MOBILE` 当成某个 workmode 枚举值
- 不要把启动期模式判定和运行期切换控制混写
- 不要把历史目标态当成当前代码事实

## 9. 推荐阅读顺序

进入该主题时建议按下面顺序：
1. `../specs/workmode-selection-and-switching.md`
2. `../decisions/workmode-vs-cmd-mobile-layering.md`
3. `../refs/workmode-code-entry-and-mode-mapping.md`
4. 本文
5. 再看：
   - `src/app/workmode/WorkMode.cpp`
   - `src/app/main_app.cpp`
   - `src/service/http_server/http_api_v1.cpp`
