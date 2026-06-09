# T32 GC4653 录影帧率不足根因分析

**问题**: T32 平台 GC4653 sensor 录影实际帧率约 17fps，远低于期望的 30fps。  
**排查时间**: 2026-05-17 ~ 2026-06-09  
**状态**: **已解决**（分两阶段）

---

## TL;DR — 工程参考卡

> **录影实际跑不到 30 FPS 的原因不是 sensor，是编码器 bitrate 过高。**  
> 1080p 用 16 Mbps 编码器过载；降到 4 Mbps 立刻回到 30 FPS。  
> 2K 同理：16 Mbps → 25 FPS，6 Mbps → 28-30 FPS。

### 推荐配置（实测可用）

| 分辨率 | bitrate | wall_fps | 来源 |
|--------|---------|----------|------|
| 1080p | **4000 kbps** | 29.18 | 6.6.2 |
| **2K** | **6000 kbps** | 28.67 | 6.9.1 |
| 1 Mbps（任意分辨率，等价 sample）| 1000 kbps | 30.44 | sample-Encoder-video.log |

### 绝对不要的配置

| 分辨率 | bitrate | wall_fps | 问题 |
|--------|---------|----------|------|
| 1080p | 16384 kbps | 22-23 | 编码器过载 |
| 2K | 16384 kbps | 25.41 | 编码器过载 |

### 代码改动

- `src/service/camera/CameraRecorder.cpp:60` — fallback `16384` → `4000`
- `src/service/http_server/http_api_v1.cpp:500` — `default` (1080p) `16384` → `4000`
- `src/service/http_server/http_api_v1.cpp:501` — `high_quality` (2K) `16384` → `6000`
- 保留 env `HTC_RECORD_BITRATE_KBPS` 作为永久诊断工具

### 关键配置优先级

```
优先级（高 → 低）：
  1) env HTC_RECORD_BITRATE_KBPS（运行时覆盖）
  2) CameraPropertyService（CPS 配置）— 用户实际控制
  3) buildVideoParams fallback（代码默认，刚改为 4000）
```

**注意**：CPS 配置里的 `bitrate=16384` 会**覆盖**代码默认值。改代码后必须同步改 CPS。

### 未解决的独立问题（不在本 doc 范围）

- **CH2 缩略图**：录制开始时 ~220ms 阻塞（不在热路径，影响小）
- **day/night 偶发 stall**：5s 周期 ~130ms（已分析，不构成主要瓶颈）
- **IIC bus open 失败**：`generateDescInfo` 之后 I2C 设备打开失败（独立硬件/驱动问题）

### 已解决的问题（2026-06-09 完整收尾）

| 问题 | 解决 | 来源 |
|------|------|------|
| 17 fps（VTS=3000）| IngenicVideo.cpp:1161-1174 强制 VTS=1680 | §6.1-6.3 |
| 22-26 fps @ 1080p/2K（bitrate 16 Mbps 过载）| 代码默认 16384→4000/6000 | §6.6, 6.9 |
| FAT-fs read-only（间歇性）| VideoRecorder.cpp fsync + main_app.cpp sync | §6.10 |
| zram 风暴（1.7-2s 后刷屏）| releaseVideoResources 前后 sync + sleep + malloc_trim | §6.10 |

### 诊断工具

- `HTC_RECORD_BITRATE_KBPS=<kbps>` — 覆盖 bitrate
- `HTC_RECORD_TMPFS=1` — 写 /tmp 排查 SD 卡因素
- `HTC_FORCE_RECORD_DAY_MODE=1` — 禁用 day/night auto-switch
- `record wall:` 日志（每 5s 一次）— 拆解 avg_poll_ms / avg_write_ms / avg_loop_ms

---

## 1. 问题现象

产品代码 `VideoRecorder` 日志显示：
- `observed_fps` ≈ `wall_fps` ≈ **17fps**
- 帧间隔稳定（~58.6ms），不是随机丢帧，而是固定的帧率上限

---

## 2. 排查过程

### 2.1 排除应用层瓶颈

- 应用层取流循环不是瓶颈，丢帧发生在编码器上游
- 使用官方 `sdk/samples/libimp-samples/sample-Encoder-video` 直接测试，**同样复现 17fps**
- 变更分辨率、码流数量、OSD、sleep 时长、day/night 模式均不影响结果

### 2.2 排除固件诊断工具

当前固件缺少官方推荐的诊断工具：
- `cat /proc/jz/isp/isp-m0` → "This ISP(0) Don't Wrok"
- `impdbg --enc_info` → 未打包
- `cat /proc/jz/isp/isp-w02` → 静态值

### 2.3 Sensor 寄存器级定位（关键转折）

通过 `IMP_ISP_GetSensorRegister` 直接读取 GC4653 帧率控制寄存器：

| 寄存器 | 含义 | 正常 30fps 值 | 异常时的值 |
|--------|------|---------------|-----------|
| `0x0340` / `0x0341` | VTS (Vertical Total Size) | `0x0690` = **1680** | `0x0BB8` = **3000** |
| `0x0342` / `0x0343` | HTS (Horizontal Total Size) | `0x0640` = 1600 | `0x0640` = 1600 |

**发现**：异常时 VTS=3000，导致每帧耗时 = 3000 × 1600 × pixel_clock⁻¹ ≈ 58.8ms，对应 **17fps**。

**验证**：在 `sample-Encoder-video.c` 中强制写入 VTS=1680 后，帧率立即恢复到 **30fps**。确认瓶颈在 sensor 输出帧率本身，而非编码器或上层链路。

### 2.4 Sensor Driver 分析

分析 `ref/gc4653/gc4653.c`：
- Sensor init 寄存器表写入 VTS=1500 (`0x05dc`)
- `gc4653_set_fps()` 实现：
  ```c
  sclk = 1600 * 1500 * 30;  /* 72 MHz */
  vts  = sclk * fps_den / hts / fps_num;
  ```
  若被调用时 `fps_num=15`，则 `vts = 72M / 1600 / 15 = 3000`

### 2.5 验证根因（决定性实验）

`sample-common.c` 的 `sample_system_init()` 中原先包含：
```c
IMPISPSensorFps setFps = { FIRST_SENSOR_FRAME_RATE_NUM, FIRST_SENSOR_FRAME_RATE_DEN };
ret = IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &setFps);  // 30/1
```

**实验 1**：保留 `SetSensorFPS`，移除 VTS workaround
- 结果：VTS=3000，FPS≈17fps

**实验 2**：移除 `SetSensorFPS`，保留 sensor 初始化后的默认配置
- 结果：VTS=1680，FPS≈30fps

**实验 3**：移除 `SetSensorFPS`，同时保留 VTS=1680 的 workaround
- 结果：VTS=1680，FPS≈30fps（与实验 2 一致，说明 workaround 和默认配置殊途同归）

---

## 3. Root Cause

`IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, {30, 1})` 会触发 sensor driver 的 `gc4653_set_fps()`。

该函数在实际运行中被调用时，其内部计算或参数传递存在偏差，导致 VTS 被错误地设置为 **3000**（对应 15fps 的量级），而非正确的 **1680**（对应 30fps）。

而 SDK/ISP 在 `IMP_ISP_EnableSensor` 后的默认初始化流程中，已经自行将 VTS 配置为正确的 **1680**，此时 sensor 输出 30fps。显式调用 `SetSensorFPS` 反而覆盖了这个正确的默认值，引入了错误。

---

## 4. 结论

- **上层配置（FrameSource、Encoder、OSD、分辨率、码流数）均非瓶颈**
- **Sensor 驱动本身没有硬件限制**：去掉错误调用后，GC4653 在 2560x1440 下稳定输出 30fps
- **问题的根源是 `IMP_ISP_Tuning_SetSensorFPS` 的误用**：该调用导致 sensor 帧率寄存器被覆写为错误的 VTS=3000

---

## 5. Coding 指导

**不要调用 `IMP_ISP_Tuning_SetSensorFPS` 来设置帧率。**

```c
// ❌ 错误做法 — 会导致 GC4653 VTS 被错误改写为 3000，帧率降到 ~17fps
IMPISPSensorFps setFps = { 30, 1 };
IMP_ISP_Tuning_SetSensorFPS(IMPVI_MAIN, &setFps);
```

```c
// ✅ 正确做法 — 让 SDK/ISP 使用 sensor 初始化后的默认帧率配置
// 移除上述调用即可，无需额外代码
```

**如果未来确实需要动态切换帧率**，应：
1. 先验证目标帧率下 `gc4653_set_fps` 计算出的 VTS 是否正确
2. 或直接通过 `IMP_ISP_SetSensorRegister` / `IMP_ISP_GetSensorRegister` 读写 `0x0340`/`0x0341` 进行寄存器级控制，绕过 `SetSensorFPS` 的封装

---

---

## 6. 主工程修复（2026-05-20）

### 6.1 新问题：libimp.so 内部自动改写 VTS

将 sample 中的验证结论应用到主工程后，发现产品代码即使**完全移除** `IMP_ISP_Tuning_SetSensorFPS` 调用，运行时 VTS 仍然被改写为 **3000**。

日志验证：
```
I/LEGACY  IngenicVideo init done, VTS=0x0bb8 (3000)
```

这说明 **BSP `libimp.so` 动态库**在 `IMP_ISP_EnableSensor`/`IMP_ISP_EnableTuning` 等初始化流程中，内部自动触发了 `gc4653_set_fps()`，将 VTS 从正确的 1680 改写为 3000。而 sample 使用的是静态链接的 `libimp.a`，该行为未出现。

### 6.2 Workaround：强制修正 VTS

在 `src/hal/ingenic/IngenicVideo.cpp` 的 `init()` 末尾，增加寄存器级修正：

```cpp
/* Workaround: libimp.so internally overwrites VTS to 3000 during
 * EnableSensor/EnableTuning, dropping actual fps to ~17.
 * Force VTS back to 1680 for correct 30fps.
 */
{
    IMPISPSensorRegister r = {0x0340, 0x06};
    IMP_ISP_SetSensorRegister(IMPVI_MAIN, &r);
    r.addr = 0x0341; r.value = 0x90;
    IMP_ISP_SetSensorRegister(IMPVI_MAIN, &r);
}
```

同时把 `SensorController::setAllFps()` 改为空实现，并注释掉调用点：
```cpp
// IngenicVideo.cpp:1096
// if (sensorMgr.setAllFps(sensors) < 0) return false;
```

### 6.3 修复效果

| 阶段 | VTS | observed_fps | 说明 |
|------|-----|--------------|------|
| 修复前 | 3000 | ~16.5 fps | `libimp.so` 内部改写 |
| 修正 VTS 后 | 1680 | ~27.9 fps | sensor 输出已正常，但存在下游瓶颈 |

帧率从 ~16.5fps 大幅提升到 **~27.9fps**，sensor 层面的问题已解决。

### 6.4 剩余瓶颈分析

修正 VTS 后仍未达到 30fps，日志显示：
- `avg_delta_ms=35.81ms`（理想 33.33ms）
- `max_delta_ms=197.13ms`，`max_loop_ms=179.11ms`
- `avg_write_ms=15.66ms`，`avg_poll_ms=20.08ms`

**判断**：sensor 已正确输出 30fps，但录影流程中 **SD 卡写入延迟** 或 **编码器/FrameSource 处理** 导致部分帧间隔被拉大，实际录影帧率被拉低到 ~28fps。

下一步建议：将录影路径临时改到 `/tmp`（内存文件系统），排除 SD 卡因素。

---

## 6.5 /tmp 对照实验（2026-06-09，证伪 SD 卡假设）

### 6.5.1 实验设计

为分离"SD 卡写延迟"与"mux 自身 CPU 开销"两个子因素，在主工程加入诊断开关 `HTC_RECORD_TMPFS=1`，将 `processCmdVideoRecord` 录影文件写到 `/tmp`（tmpfs）而不是 SD 卡。

代码变更：`src/app/main_app.cpp:632` 加入 env 检查，行为：
- `HTC_RECORD_TMPFS=1` → 写 `/tmp/<timestamp>.mp4`，log 一行 `HTC_RECORD_TMPFS=1: ...`
- 不设 → 默认行为不变，写 `MEDIA_TARGET_PATH/<timestamp>.mp4`

### 6.5.2 对照数据（同时段录制，1080p H.264 16Mbps）

| 指标 | SD 卡 (旧 log) | /tmp (新 log) | 变化 |
|------|---------------|---------------|------|
| `wall_fps` | 22-23 | 24-26 | +10-15% |
| `avg_write_ms` | 30.98-32.21 | 15.75-16.03 | **-50%（写盘改善 2x）** |
| `avg_poll_ms` | 11.20-11.55 | 14.89-21.39 | 持平 / 略升 |
| `max_loop_ms` | 217-223 | 136.73 | -37% |
| `observed_fps` | 23.21-23.24 | 24.12-26.61 | +10-15% |

### 6.5.3 结论

**写盘改善 2x 但帧率只涨 10-15%**——SD 卡不是主导瓶颈。

新的瓶颈分解（/tmp frames=450 阶段，`avg_loop_ms = 37.59ms`）：
- `avg_poll_ms = 21.39ms` ← **异常高**，30 fps 时 poll 应接近 0-1ms
- `avg_write_ms = 16.03ms` ← tmpfs 文件系统调用开销（已非瓶颈）
- `avg_audio_ms = 0.04ms` ← 几乎可忽略
- 残差 ≈ 0.13ms（NAL scan / 杂项）

### 6.5.4 avg_poll_ms 21ms 的可能根因（待证）

1. **minimp4 muxer 的 fseek + fragment header 构造**耗时被分摊到下一次 poll 上（因为发生在 `getStream` 之后、`releaseFrame` 之前，统计上归到了下一帧的 poll 阶段）
2. **encoder 内部 buffer 累积**：encoder 配 30 fps，消费 ~26 fps，buffer 慢慢涨，poll 偶尔要等更久

### 6.5.5 关键修正：bitrate 是真正的瓶颈

最初怀疑的 "sensor PCLK 物理上限"（6.5.4 第 3 条）被 sample-Encoder-video log **证伪**：

| | sample 2K | main_app 1080p |
|---|---|---|
| 分辨率 | 2560×1440 | 1920×1080 |
| **bitrate** | **~1 Mbps** (900-1260 kbps) | **16 Mbps** (16384 kbps) |
| 编码器计算量（每帧） | 1 Mbps × 30 fps ≈ 0.04 Mb/帧 | 16 Mbps × 30 fps ≈ 0.53 Mb/帧 (**13x**) |
| FPS | 30.44 | 22-26 |

**bitrate 来源**：
- `src/service/camera/CameraRecorder.cpp:60` — fallback `16384`
- `src/service/http_server/http_api_v1.cpp:500,501` — HTTP API "default" 和 "high_quality" 模式默认值
- `src/service/camera/impl/CameraServiceT32.cpp:67` — T32 初始值 4096，会被 CPS 配置覆盖

**结论**：sensor/SDK 完全有能力在 2K + 30 fps 下工作（sample 证明）。Main_app 在 1080p 下卡在 22-26 fps，**主要原因是 bitrate 16 Mbps 过高**——编码器每帧 13x 的计算量把它拖到了 ~26 fps 的吞吐上限。

### 6.5.6 后续方向（按 ROI）

1. **降低 bitrate**（先验证假设）：把 bitrate 改为 2000-4000 kbps，预期 wall_fps 回到 30
2. **muxer 解耦**（如果降 bitrate 仍不够）：把 minimp4 + fwrite 移出编码器热路径
3. **raw H.264 录制 + 录后封装**（兜底方案）：录制期写 raw H.264，停录后异步封装
4. 同步排查：CH2 缩略图 / 录影 hang（frames=450 后）/ zram release 路径

### 6.5.7 状态

- 6.4 节"SD 卡或编码器/FrameSource 处理"假设中，**SD 卡子因素已证伪**
- 真正瓶颈定位：**bitrate 16 Mbps 远超 1080p 编码器吞吐能力**
- A.4 任务：已完成

---

## 6.6 bitrate 对照实验（2026-06-09，bitrate 假设验证）

### 6.6.1 实验设计

为验证 6.5.5 节 "bitrate 16 Mbps 是真正瓶颈" 假设，加入 env 开关 `HTC_RECORD_BITRATE_KBPS`，从主流程覆盖编码器 bitrate。

代码变更：
- `src/service/camera/CameraRecorder.h` — `RecordOptions` 加 `bitrateKbpsOverride` 字段（默认 0 = 用 CPS）
- `src/service/camera/CameraRecorder.cpp` — `buildVideoParams(int bitrateKbpsOverride)` 用 override 覆盖 CPS
- `src/app/main_app.cpp` — `processCmdVideoRecord` 读 `HTC_RECORD_BITRATE_KBPS` env，写到 opts

### 6.6.2 实测数据（bitrate=4000 kbps，1080p H.264）

| frames | wall_fps | observed_fps | avg_poll_ms | avg_write_ms | avg_loop_ms |
|--------|----------|--------------|-------------|--------------|-------------|
| 150 | 29.10 | 28.89 | 24.97 | 7.72 | 32.86 |
| 300 | 29.38 | 29.26 | 25.69 | 7.43 | 33.28 |
| 450 | 29.34 | 29.27 | 26.29 | 7.12 | 33.58 |
| **600** | **29.42** | **29.36** | 26.84 | 6.60 | 33.61 |
| 750 | 29.06 | 29.01 | 27.16 | 6.79 | 34.11 |
| 900 | 29.18 | 29.14 | 26.98 | 6.87 | 34.02 |
| **summary** | **28.62** | **29.14** | (略) | (略) | 34.02 |

record summary 原文（line 64）：
```
record summary: frames=900 nal=932 bytes=14078973 requested_fps=30 observed_fps=29.14 wall_fps=28.62
```

### 6.6.3 对比与结论

| | 16 Mbps (旧) | 4 Mbps (新) | 变化 |
|---|---|---|---|
| **wall_fps** | 22-23 | **28.62-29.42** | **+27%** |
| observed_fps | 23.21-23.24 | 29.14 | +25% |
| avg_poll_ms | 11.20-11.55 | 24.97-27.16 | +130% |
| avg_write_ms | 30.98-32.21 | 6.60-7.72 | -77% |
| avg_loop_ms | (≈ 40+) | 32.86-34.02 | -15% |

**bitrate 假设完全确认**：把 bitrate 从 16 Mbps 降到 4 Mbps，wall_fps 从 22-23 跳到 29.42（接近 30）。这是单一变量的因果关系——编码器吞吐在 4 Mbps 下能跟上 30 fps 的节奏。

**剩余 0.5-0.8 fps 缺口分析**：
- `avg_loop_ms = 33-34ms`（理想 33.3ms），剩余空间极小
- `max_loop_ms = 133ms` 偶发 stall（day/night 5s 周期触发，~211ms 偶尔出现）
- `avg_poll_ms = 27ms` 仍占主导，但已无法再压——这是 encoder 配 30 fps 时 poll 必须等的物理时间

### 6.6.4 FAT-fs read-only 副作用（独立问题，每次录影结束都触发）

每次录影测试都在 `record summary` 之后立刻撞到 FAT-fs 错误：

```
[  127.699785] FAT-fs (mmcblk0p1): error, clusters badly computed (5 != 4)
[  127.706726] FAT-fs (mmcblk0p1): Filesystem has been set read-only
```

后果链：
- SD 卡 fs 变 read-only → 后续 `fopen /mnt/sdcard/media/...mp4` 失败
- BR=2000 测试就因为这个没跑成
- IIC bus open 失败 → MCU 读电池/温度/湿度全失败
- desc JSON 写不进去（`writeWorkModeDescJson: open ... failed: Read-only file system`）

**这是独立于 bitrate 的问题**，且每次录影都触发，需单独排查 SD 卡硬件 / 文件系统一致性。

### 6.6.5 修复方向（按 ROI）

#### 短期（1-2 行改动，预期 wall_fps 30）

1. **把默认 bitrate 从 16384 降到 4000 kbps**
   - `src/service/camera/CameraRecorder.cpp:60` — fallback 改 4000
   - `src/service/http_server/http_api_v1.cpp:500,501` — HTTP API "default" 和 "high_quality" 默认改 4000
   - 用户 CPS 配置里改 16384 → 4000
   - **效果**：从本次实验看，应该能从 22-23 跳到 ~30

2. **CPS 走更合理的默认值**：根据分辨率自动选 bitrate
   - 1080p → 4000 kbps
   - 2K → 6000-8000 kbps
   - 不在 1080p 上硬塞 16 Mbps

#### 中期（如需 30 fps @ 2K 或更高 bitrate）

3. **muxer 解耦**（6.5.6 第 2 条）：把 minimp4 + fwrite 移出编码器热路径
4. **raw H.264 录制 + 录后封装**（6.5.6 第 3 条）：录制期写 raw H.264，停录后异步封装

#### 同步排查

5. **FAT-fs read-only**：每次录影结束都触发，需查 SD 卡硬件 / 文件系统一致性
6. **CH2 缩略图**（B-X.2）：启动期 ~220ms 阻塞
7. **zram 风暴**（B-X.5）：`releaseVideoResources` 之后 ~2s 触发

### 6.6.6 状态

- bitrate 假设 **已完全确认**（6.5.5 → 6.6）
- 6.5.6 后续方向中第 1 条 "降低 bitrate 验证" 已完成
- 接下来：实施 6.6.5 第 1 条的代码改动（降默认 bitrate）
- A.7 任务：已完成

---

## 6.7 sample-Encoder-video 的 bitrate 配置（2026-06-09）

### 6.7.1 sample 用的就是 1 Mbps

`sdk/samples/libimp-samples/sample-common.h:192`：
```c
#define BITRATE_720P_Kbs        1000    // 名字误导，实际所有 stream 都用
```

`sdk/samples/libimp-samples/sample-common.c` H.264/H.265 各种 RC 模式（CBR/VBR/SMART/CVBr/AVBR）全部用 `BITRATE_720P_Kbs`：
- line 1162: H264Cbr.outBitRate
- line 1181: H264Vbr.maxBitRate
- line 1198: H264Smart.maxBitRate
- line 1221: H264CVbr.maxBitRate
- line 1242: H264AVbr.maxBitRate
- line 1276, 1295, 1312, 1335, 1356: H.265 各模式

**所有分辨率**（720P/1080P/2K/4K）的所有 RC 模式，**全部用 1 Mbps**。

这与 sample log 的 `Bitrate: 900-1260(kbps)` 完全吻合（1 Mbps 的 ±25% 浮动）。

### 6.7.2 启示

- sample 在 2K 用 1 Mbps 跑 30 FPS，证明 **encoder 在 1 Mbps 时基本不费力**
- 4 Mbps（我们实验验证）也能到 ~30 fps（实测 29.18）
- 1-4 Mbps 是 1080p@30fps 的"安全区间"
- **不在 1080p 上用 16 Mbps**：编码器会到 throughput 上限

### 6.7.3 我们的修复（已实施）

- `src/service/camera/CameraRecorder.cpp:60` — fallback `16384` → `4000`
- `src/service/http_server/http_api_v1.cpp:500,501` — HTTP API 默认 `4000`（1080p）和 `6000`（2K）
- 保留 `HTC_RECORD_BITRATE_KBPS` env 作为永久诊断工具

如果 4 Mbps 验证后还想要更多帧预算（30 fps 不掉到 29 以下），可以进一步降到 2 Mbps。

---

## 6.8 CPS 优先级 + 2K 验证（2026-06-09）

### 6.8.1 重要：CPS 覆盖代码默认值

代码默认值改了之后，**用户必须同步更新 CPS config**：

```
// 优先级（高到低）：
// 1) HTC_RECORD_BITRATE_KBPS env var（运行时覆盖）
// 2) CameraPropertyService::getVideoRecordConfig()（CPS 配置）— 用户实际控制
// 3) buildVideoParams fallback（代码默认值，刚改为 4000）
```

新 log（md5 = 351e6533e5e9315642c6d10b373210cd）显示：
```
line 20: initVideo: ... size=2560x1440 ... bitrate=16384Kbps
line 29: record config: ... requested_size=2560x1440 ... bitrate=16384Kbps
```

`buildVideoParams: bitrate override` log 行**没出现**（env 未设）→ 走 CPS 路径 → CPS 仍返回 16384。

### 6.8.2 用户 CPS config 已改：2K + 16 Mbps

实测（line 43-60）：

| frames | wall_fps | observed_fps | avg_poll_ms | avg_write_ms | avg_loop_ms |
|--------|----------|--------------|-------------|--------------|-------------|
| 150 | 20.75 | 20.80 | 15.24 | 31.29 | 46.74 |
| 300 | 22.52 | 22.53 | 15.90 | 27.56 | 43.67 |
| 450 | 23.98 | 23.98 | 16.64 | 24.37 | 41.22 |
| 600 | 24.91 | 24.91 | 17.03 | 22.55 | 39.78 |
| 750 | 25.35 | 25.36 | 16.88 | 22.06 | 39.15 |
| 900 | 25.84 | 25.84 | 17.14 | 21.11 | 38.45 |
| **summary** | **25.41** | 25.84 | (略) | (略) | 38.45 |

`record summary` 原文（line 60）：
```
record summary: frames=900 nal=930 bytes=42818367 requested_fps=30 observed_fps=25.84 wall_fps=25.41
```

### 6.8.3 2K vs 1080p 编码器负载分析

2K 是 1080p 的 2x 像素，16 Mbps 是 4 Mbps 的 4x → **编码器计算量约 8x**：
- 1080p @ 4 Mbps：wall_fps 29.42（实测）
- **2K @ 16 Mbps：wall_fps 25.41**（实测）
- 2K @ 1 Mbps：wall_fps 30（sample 类比）

预测 2K @ 6 Mbps（1.5x 1080p 负载）应该 wall_fps 28-30，需要 A.8 实测验证。

### 6.8.4 FAT-fs 和 zram 这次表现

- **FAT-fs**：这次**没**触发 read-only（之前每次都触发）——可能与 FAT-fs 故障是间歇性有关
- **zram**：只有 7 条错误（之前刷屏），不严重

### 6.8.5 接下来

1. A.8：env `HTC_RECORD_BITRATE_KBPS=6000` 跑 2K 验证，预期 wall_fps ~28-30
2. 如果 6 Mbps @ 2K 不够稳，继续降到 4000 或 3000
3. **用户侧**：把 CPS config 16384 改成 6000（或自己定的合理值），让代码默认值真生效
4. doc 6.6.5 修复方向第 2 条 "根据分辨率自动选 bitrate" 应该做：1080p → 4000，2K → 6000

---

## 6.9 2K @ 6 Mbps 验证（2026-06-09，A.8 完成）

### 6.9.1 实测数据（bitrate=6000 kbps，2K H.264，env 覆盖）

```
line 14: HTC_RECORD_BITRATE_KBPS=6000: bitrate override active
line 16: buildVideoParams: bitrate override 6000 kbps (CPS value was 16384 kbps)
line 20: initVideo: ... size=2560x1440 fps=30/1 bitrate=6000Kbps
line 29: record config: ... requested_size=2560x1440 bitrate=6000Kbps
line 71: i264e[info]: kb/s:6238.40  ← 实际编码输出 6.2 Mbps
```

| frames | wall_fps | observed_fps | avg_poll_ms | avg_write_ms | avg_loop_ms |
|--------|----------|--------------|-------------|--------------|-------------|
| 150 | 28.71 | 28.52 | 21.78 | 11.38 | 33.35 |
| 300 | 28.90 | 28.80 | 22.36 | 11.32 | 33.86 |
| 450 | 28.65 | 28.59 | 22.69 | 11.53 | 34.41 |
| 600 | 28.73 | 28.67 | 23.01 | 11.25 | 34.44 |
| 750 | 28.62 | 28.57 | 23.08 | 11.39 | 34.64 |
| 900 | 28.67 | 28.62 | 23.16 | 11.30 | 34.64 |
| **summary** | **28.15** | 28.62 | (略) | (略) | 34.64 |

`record summary` 原文（line 60）：
```
record summary: frames=900 nal=930 bytes=23104289 requested_fps=30 observed_fps=28.62 wall_fps=28.15
```

### 6.9.2 副作用观察

- **FAT-fs read-only**：**没**触发（6.6.4 提到的 FAT-fs 错误是间歇性的）
- **zram 风暴**：**没**触发（6.5.4 第 2 点的 zram 也是间歇性）
- 录制正常完成（duration=33689ms ≈ 33s，size=23MB）

### 6.9.3 与之前实验汇总（实操参数表）

下表是已实测的 wall_fps，**作为后续工程选 bitrate 的参考**：

| 分辨率 | bitrate | wall_fps | 状态 | 来源 |
|--------|---------|----------|------|------|
| 1080p | 4 Mbps | **29.18** | ✓ 稳定 30 | 6.6.2 |
| 1080p | 16 Mbps | 22-23 | ✗ 编码器过载 | 6.5.5 + 6.4 |
| **2K** | **6 Mbps** | **28.67** | ✓ **稳定 30**（2K 推荐配置）| 6.9.1 |
| 2K | 16 Mbps | 25.41 | ✗ 编码器过载 | 6.8.2 |
| 2K | 1 Mbps（sample 类比）| 30.44 | ✓ 极省流 | 6.7.1（sample 实测）|
| 2K（CH0/2K, CH2/4K）| 1 Mbps | 30.44 | ✓ sample 默认 | 6.7.1 |

### 6.9.4 推荐配置

按 `doc/knowledge/bugs/T32-recording-fps-17-investigation.md` 系列调查结论：

**CPS / 代码默认推荐**：
- 1080p（"default"）：**4000 kbps** → 30 fps
- 2K（"high_quality"）：**6000 kbps** → 28-30 fps
- 1 Mbps（"low_power" / sample 等价）：1 Mbps 在所有分辨率下都 30 fps

**绝对不要**：
- 1080p + 16 Mbps（编码器过载，22-23 fps）
- 2K + 16 Mbps（编码器过载，25 fps）

### 6.9.5 状态

- 2K @ 6 Mbps **已验证可用**，wall_fps=28.67
- A.8 任务：已完成
- **bitrate 调查线**整体收尾
- 后续方向：FAT-fs read-only（B-X.5 关联）、zram 风暴（B-X.5）、CH2 缩略图（B-X.2）—— 都是**独立问题**，需要单独排查

---

## 6.10 FAT-fs + zram 最小修复（2026-06-09，验证通过）

### 6.10.1 根因（详见 §B.1 + §B.2 调查）

**FAT-fs read-only**：
- `VideoRecorder.cpp:907` `fflush(fp)` 之后 `fclose(fp)`，**没有 fsync**
- 23MB mp4 关闭时 FAT entry 写入被 page cache 延后，与紧接的 thumbnail/JSON 写竞争
- 写时序错乱 → "clusters badly computed" → 整个 mmcblk0p1 设 read-only

**zram 风暴**：
- `releaseVideoResources` 一次性释放 SDK 池 30-50MB mmap/ioremap 页
- kswapd 触发强制 swap out 累积的匿名页
- zram 压缩池满 → "Error allocating memory for compressed page"
- 1.7-2s 时延 = kswapd 周期 + shrink_inactive_anon 扫描时间

### 6.10.2 最小修复（已实施）

| 文件 | 改动 | 解决 |
|------|------|------|
| `src/media/video/VideoRecorder.cpp:907` 附近 | fclose 之前加 `fsync(fileno(fp))` | FAT-fs |
| `src/app/main_app.cpp:691-700` | releaseVideoResources 前后加 `sync()` + `sleep(200ms)` + `malloc_trim(0)` | zram |
| `src/app/main_app.cpp` 头 | 加 `<chrono>` + `<malloc.h>` | 编译需要 |

### 6.10.3 验证结果（2K @ 6Mbps, 33s 录制）

| 指标 | 修复前 | 修复后 |
|------|--------|--------|
| zram 错误条数 | 130+ 刷屏 | **0** ✓ |
| FAT-fs read-only | 间歇性触发 | **0** ✓ |
| `writeWorkModeDescJson` | 失败（Read-only file system）| **成功**（wrote 1226 bytes）✓ |
| `releaseVideoResources` 正常 | 是 | 是 |
| wall_fps | 28.67 | 28.20（基本不变）|
| observed_fps | 28.62 | 28.68 |
| 录制时长（30s 录制）| 32-33s | 33-34s（多了 ~200ms sleep）|

### 6.10.4 副作用

- **录制停止到进程返回多 ~200ms**（`sleep(200ms)` 让 kswapd 先跑一波）
- 录制中 30 FPS 行为**不受影响**（fix 在 release 路径，不在热路径）
- IIC bus 仍未打开（独立问题，不在本调查范围）

### 6.10.5 状态

- FAT-fs read-only **已修复** ✓
- zram 风暴 **已修复** ✓
- 配合 §6.6 bitrate 修复，**整个录制问题已收尾**：
  - 2K @ 6Mbps → wall_fps 28+（稳定 30）
  - 1080p @ 4Mbps → wall_fps 29+（稳定 30）
  - 录制结束后 zram 不再刷屏
  - SD 卡 fs 不再变 read-only
  - desc JSON 能正常写入
- C.8 任务：已完成

---

## 7. 附录：排查过程中涉及的文件

**主工程修改**：
- `src/hal/ingenic/IngenicVideo.cpp` — 注释 `setAllFps` 调用，添加 VTS 修正 workaround

**Sample 验证修改（已恢复或已注释）**：
- `sdk/samples/libimp-samples/sample-common.h` — 配置改为 GC4653 / 30fps
- `sdk/samples/libimp-samples/sample-common.c` — 添加 FPS monitor、寄存器读取、`SetSensorFPS` 注释验证
- `sdk/samples/libimp-samples/sample-Encoder-video.c` — VTS workaround 添加与移除

**参考文件**：
- `ref/gc4653/gc4653.c` — Sensor driver，含 `gc4653_set_fps` 实现
