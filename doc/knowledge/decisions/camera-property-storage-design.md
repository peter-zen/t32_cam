# 相机属性存储方案

> 更新日期：2026-05-03。本文已从原始设计文档更新，反映 CPS-CS-SET1 registry 模型的完整实现。

## 核心决策

### 1. 不新增 SQLite 配置库

- `media_file.db` 只存媒体元数据
- `media_thumb.db` 只存缩略图
- 相机属性不使用 SQLite 存储

### 2. Schema + Store 分层

| 层 | 职责 | 实现 |
|---|---|---|
| `CameraParameterRegistry` | 声明式参数目录（100+ 参数，4 种分类） | `CameraParameterRegistry.cpp` |
| `CameraPropertyService` | 校验、读写、持久化、JSON 导出 | `CameraPropertyService.cpp` (1667 行) |
| `Settings` (JSON) | 运行时业务设置持久化 | `Settings.h/cpp` |
| `DeviceConfig` (INI) | 设备配置持久化 | `DeviceConfig.h/cpp` |

### 3. 属性候选值不存数据库

候选值由代码运行时根据机型能力生成（如 `MVideo` 配置决定可选分辨率），不入库。

## 两层属性体系

### Legacy 层 (8 个属性)

原始硬编码属性，通过 `Settings` 结构体直接读写：

| 属性名 | 存储 |
|---|---|
| `resolution`, `fps` | `Settings.videoSize` |
| `bitrate` | `Settings.bitRate_*k` |
| `pir_enabled` | `Settings.pirEn` |
| `pir_sensitivity` | `Settings.ckPirSensitivity` |
| `loop_recording` | `Settings.autoCover` |
| `max_record_duration` | `Settings.videoLength` |
| `timestamp_overlay` | `Settings.stampEn` |

### Registry 层 (CPS-CS-SET1, 100+ 参数)

声明式目录，按 CPS 客户规范定义，支持 4 种分类：

| 分类 | 权限 | 存储源 | 数量 |
|---|---|---|---|
| `FACTORY` | 只读/工厂权限 | `DeviceConfig` (INI) | ~30 |
| `PROPERTY` | 读写 | `Settings` (JSON) / `DeviceConfig` (INI) | ~80 |
| `STATUS` | 只读 | `computed` / `placeholder` | ~30 |
| `COMMAND` | 命令 | N/A | 少量 |

每个参数定义包含：`id`, `rawName`, `displayName`, `classification`, `group`, `type`, `permission`, `defaultValue`, `options`, `range`, `dependency`, `storageBinding`, `availability`。

## 存储绑定类型

| `storageBinding.kind` | 含义 | 状态 |
|---|---|---|
| `settings` | 绑定到 `Settings` JSON 结构体成员 | 可读写 |
| `device_config` | 绑定到 `DeviceConfig` INI section/key | 可读写 |
| `computed` | 运行时计算（如 `fw_version`） | 只读 |
| `placeholder` | 已知字段但未实现绑定 | 返回默认值，写入拒绝 |

## 依赖系统

参数可依赖一个 "开关" 参数。例如：
- `CAM_Ffixed_Shutter` 依赖 `Photo_DS_EN`（照片详细设置开关）
- `Timer_4Start/End` 依赖 `Timer_Range_MAX`（时间段数量限制）

`isParameterEnabled()` 检查开关参数的期望值，禁用时返回 `enabled: false` 和 `disabled_reason`。

## 架构流程

```
HTTP API → CameraPropertyService
  → CameraParameterRegistry (查找定义)
  → Settings / DeviceConfig (读写当前值)
  → CameraService (运行时应用，如需)
```

## 约束

- `resolution` 与 `fps` 耦合到同一个 `videoSize`，单独设其中一个可能带动另一个变化
- `src/hal/**` 不在属性服务范围内
- PLACEHOLDER 参数写入时返回 `storage_not_implemented` 错误
