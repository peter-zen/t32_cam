# T32 GC4653 录影帧率不足排查记录

**问题**: T32 平台 GC4653 sensor 录影实际帧率约 17fps，远低于期望的 30fps。
**排查时间**: 2026-05-17
**状态**: 未解决，根因定位到 Sensor/ISP 驱动层

---

## 1. 初始分析（应用层日志）

产品代码 `VideoRecorder` 日志显示：
- `observed_fps` ≈ `wall_fps` ≈ 17fps
- 帧间隔稳定（~58.6ms）

**结论**: 应用层取流循环不是瓶颈，丢帧发生在编码器上游。

---

## 2. 尝试使用内核/驱动诊断工具

根据 `ref/T32 录影帧率不足排查指南（ISP vs Encoder 丢帧定位）.md`：
- `cat /proc/jz/isp/isp-m0` → 返回 "This ISP(0) Don't Wrok"，固件中该节点不可用
- `impdbg --enc_info` → 固件中未打包此工具
- `cat /proc/jz/isp/isp-w02` → 静态值，不刷新

**结论**: 当前固件缺少官方推荐的诊断工具，无法直接从内核确认 sensor 实际帧率。

---

## 3. SDK Sample 对比验证

修改 `sdk/samples/libimp-samples/sample-common.h/c` 适配当前产品配置：
- Sensor: sc4336p → **gc4653**
- 帧率: 15fps → **30fps**
- 编码: H265 → **H264**
- 通道: 单码流 → **双码流** (2560x1440 + 1280x720)
- 输出路径: `/tmp`
- 录制帧数: 120 帧（约 4 秒）

### 3.1 测试结果汇总

| 测试项 | 配置 | 结果 |
|--------|------|------|
| sample-Encoder-video | 双码流 H264, 2560x1440, sleep(5) | **17.05 fps** |
| sample-Encoder-OSD | 双码流 H264, 2560x1440, gosd=3 | **17.05 fps** |
| sample-Encoder-video-nosleep | 双码流 H264, 2560x1440, 无 sleep | **17.05 fps** |
| sample-Encoder-video (单码流) | 仅 chn0, 2560x1440 | **17.05 fps** |
| sample-Encoder-video (1080P) | 仅 chn0, 1920x1080 | **17.05 fps** |
| sample-Encoder-video (day 模式) | IR cut 强制 day | **17.05 fps** |

### 3.2 关键发现

- **sleep(5) 不是原因**: nosleep 版本同样是 17fps
- **OSD 绑定路径不是原因**: OSD sample 与 video sample 帧率相同
- **双码流不是原因**: 单码流同样是 17fps
- **分辨率不是原因**: 1080P 单码流同样是 17fps
- **IR cut 不是原因**: day/night 模式切换不影响帧率
- **PotPlayer 显示 30fps 是误导**: 播放器读取的是编码器元数据（声明帧率），不是实际帧率。实际帧率以 `observed_fps` 或 `总帧数/总时长` 为准

---

## 4. Sensor FPS API 验证

在 `sample_system_init()` 中添加 `IMP_ISP_Tuning_GetSensorFPS` 调用：

```
[SENSOR-FPS] actual sensor fps: 30/1
```

**注意**: 该 API 返回 30/1，但结合编码器实际输出 17fps 的事实，此返回值很可能只是读取了之前 `SetSensorFPS` 设置的**期望值**，而非从硬件 VSYNC 实际测量得到的帧率。固件中缺少验证手段。

---

## 5. 最终结论

1. **Sensor 驱动或 ISP 固件层存在系统性限制**，导致 GC4653 实际输出帧率被锁定在约 17fps
2. 所有上层配置（FrameSource、Encoder、OSD、绑定路径、分辨率、码流数量、day/night 模式）均非瓶颈
3. 帧间隔极其稳定（58.6ms ± 0.2ms），说明这不是随机丢帧，而是**固定的帧率上限**
4. 旧版 `sample-common.h.bak` 中 sc4336p 配置为 15fps，与此现象有相似性（均无法达到 30fps）

---

## 6. 下一步建议

1. **联系 BSP/Ingenic 技术支持**
   - 确认 T32 平台 + GC4653 在 2560x1440 下的实际最高支持帧率
   - 检查 sensor 驱动中 `default_boot=0` 对应的寄存器表是否确实配置为 30fps
   - 确认 `IMP_ISP_Tuning_GetSensorFPS` 在固件中是否从硬件测量，还是仅返回设置值

2. **替代验证方案**
   - 在相同 T32 平台上测试其他 sensor（如 sc4336p、gc5613），确认是 GC4653 驱动特有还是平台全局限制
   - 如果其他 sensor 在 1080P/2K 下能达到 30fps，则说明问题在 GC4653 驱动

3. **产品层临时方案**
   - 如果 BSP 确认无法支持 30fps，产品代码应将录影帧率期望从 30fps 下调到 15fps 或 17fps，避免与实际帧率不匹配导致的编码器/播放器异常

---

## 附录：排查过程中修改的文件

以下文件在本次排查中被临时修改，排查结束后已 rollback：
- `sdk/samples/libimp-samples/sample-common.h`
- `sdk/samples/libimp-samples/sample-common.c`
- `sdk/samples/libimp-samples/sample-Encoder-video.c`

新增临时文件（未纳入版本控制）：
- `sdk/samples/libimp-samples/sample-Encoder-video-nosleep.c`
- 多个临时编译产物（`.o`, 测试二进制）
