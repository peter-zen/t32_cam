# Device / Storage HTTP 探针操作手册

## 1. 目的

给后续会话提供一条最小充分、可重复的 device / storage 验证路径，用于：
- 确认 V1 路由仍然稳定存在
- 确认当前 JSON 结构与示例字段
- 避免把返回值误当成已接真实硬件数据

这篇文档是操作手册，不是行为规格。请先结合：
- `../specs/device-and-storage-surface-behavior.md`
- `../decisions/device-and-storage-http-surface-vs-real-backend-depth.md`
- `../refs/device-storage-code-entry-and-field-source-notes.md`

## 2. 适用范围

适用于：
- HTTP V1 表层探针验证
- 字段结构确认
- 文档/客户端联调初期检查

不适用于：
- 证明当前值来自真实硬件
- 证明格式化闭环已经接线完成

## 3. 当前推荐验证项

### 3.1 `device/info`
```bash
curl http://127.0.0.1:8080/api/v1/device/info
```

重点确认：
- `code=0`
- `pid`
- `camera_ver`
- `camera_model`
- `camera_build`
- `mcu_ver`

### 3.2 `device/sensors`
```bash
curl http://127.0.0.1:8080/api/v1/device/sensors
```

重点确认：
- `battery`
- `battery_level`
- `battery_type`
- `ext_power`
- `sdcard_capacity`
- `sdcard_used`
- `datetime`

### 3.3 `storage/info`
```bash
curl http://127.0.0.1:8080/api/v1/storage/info
```

重点确认：
- `total`
- `free`
- `used`

### 3.4 `storage/format`
```bash
curl -X POST http://127.0.0.1:8080/api/v1/storage/format \
  -H 'Content-Type: application/json' \
  -d '{}'
```

重点确认：
- `accepted=true`
- `status="scheduled"`

## 4. 当前验证结论该怎么写

如果只做了上述 HTTP 探针，你最多只能写：
- 路由存在
- JSON 结构稳定
- 返回字段与当前代码一致

你不能写：
- 电池值已来自 MCU 实时采样
- sdcard 容量已来自真实磁盘统计
- storage format 已真实执行格式化

## 5. 推荐排查顺序

### 5.1 想确认字段是不是写死的
先看 `http_api_v1.cpp` 里的 `build_*_json()`。

### 5.2 想确认仓库里是否已有真实底层能力
再看：
- `Disk.cpp`
- `MCU.cpp`

### 5.3 想确认 HTTP 是否已接到底层能力
回头检查 handler 是否真的调用了这些底层接口。
没有调用，就不要脑补有隐藏接线。

## 6. 常见错误理解

- 不要把示例数值当成实时数值
- 不要把 accepted/scheduled 当成真实执行完成
- 不要把“底层有能力”写成“V1 已接线”
- 不要把 device / storage 域成熟度写成和 camera 域一样

## 7. 推荐阅读顺序

进入该主题时建议按下面顺序：
1. `../specs/device-and-storage-surface-behavior.md`
2. `../decisions/device-and-storage-http-surface-vs-real-backend-depth.md`
3. `../refs/device-storage-code-entry-and-field-source-notes.md`
4. 本文
5. 再看：
   - `src/service/http_server/http_api_v1.cpp`
   - `src/hardware/disk/Disk.cpp`
   - `src/hardware/mcu/MCU.cpp`
