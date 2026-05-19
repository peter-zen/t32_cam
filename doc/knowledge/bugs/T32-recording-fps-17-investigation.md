# T32 GC4653 录影帧率不足根因分析

**问题**: T32 平台 GC4653 sensor 录影实际帧率约 17fps，远低于期望的 30fps。  
**排查时间**: 2026-05-17 ~ 2026-05-19  
**状态**: **已解决** — 根因定位到 `IMP_ISP_Tuning_SetSensorFPS` 误用  

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

## 6. 附录：排查过程中涉及的文件

**临时修改用于验证（已恢复或已注释）**：
- `sdk/samples/libimp-samples/sample-common.h` — 配置改为 GC4653 / 30fps
- `sdk/samples/libimp-samples/sample-common.c` — 添加 FPS monitor、寄存器读取、`SetSensorFPS` 注释验证
- `sdk/samples/libimp-samples/sample-Encoder-video.c` — VTS workaround 添加与移除

**参考文件**：
- `ref/gc4653/gc4653.c` — Sensor driver，含 `gc4653_set_fps` 实现
