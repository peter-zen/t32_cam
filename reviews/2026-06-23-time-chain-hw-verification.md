# 2026-06-23 — 时间链 time_test 真机验证（Stage 1）

> 把 wm-app-spec §6/§7 的时间链（RTC→MCU→NTP + ntpSynced + 关机回写）作为独立 `time_test` 骨架（`src/app/time_test.cpp`，照搬 snap_test）先写出来，上 T32 真机 1-boot-1-case 验证——收口 spec §10/§11 标的最高风险段（mcu/ntp 此前仅 sim-green）。
> 验证驱动：`tests/host/test_time_chain.py`（7 离线 case）。

## 结论

**Stage 1 全绿（7/7 passed in 23.68s）。时间链在 T32 真机验证通过。** 链代码（`acquireTimeChain` / `writebackMcuTime`）可 lift 进 wm 启动路径。

## 验证矩阵

| case | 验证点 | 结果 |
|------|--------|------|
| `rtc-read` | RTC::getTime 读 + read-implies-set 探测 | ✅ rtc_ok=1, plausible, clobber=0 |
| `rtc-roundtrip` | RTC::setTime→getTime 往返 | ✅ write_ok=1, match=1 |
| `mcu-read` | MCU::getDatetime（I2C 读） | ✅ 可读（zeroed=0，I2C 工作） |
| `mcu-roundtrip` | MCU::setDatetime→getDatetime 往返 | ✅ write_ok=1, match=1 |
| `chain-runs` | 全链跑通、source 合法 | ✅ source ∈ 合法集 |
| `writeback-synced` | ntp_synced=1→用 sys→写 MCU | ✅ ntp_tried=0, mcu_written=1 |
| `writeback-unsynced` | ntp_synced=0→先 NTP→写 MCU | ✅ ntp_tried=1, **ntp_ok=1**, mcu_written=1 |
| （手动）RTC 兜底 | 冷启 sys=1970→链选 rtc | ✅ sys_before=1970 → source=rtc, sys_after=2026 |

## 关键发现（对 wm 设计/spec 有影响）

1. **NTP 在真机工作（Stage 2 风险基本免费收口）**：`busybox ntpd -p www.aidetcloud.com:123` 在公司网可达、能同步。原以为 NTP 仅 sim-green、真机未知——现已确认真机可同步。`writeback-unsynced` 的 `ntp_ok=1` 即证。**spec §10 的 NTP 验证状态可从 🟡 更新。**

2. **MCU 时间陈旧**：本机 MCU 读出 `2021-01-01`（plausible=0，但 zeroed=0，I2C 本身工作）。即 MCU 未被预设到当前时间。含义：冷启时 MCU 兜底帮不上（implausible），链实际靠 RTC。**关机回写路径（writeback）会把准时间写回 MCU，逐步修正**——设计自洽。

3. **内核会 sys←RTC 同步**：`settimeofday(1970)` 在 RTC plausible 时被内核立即回滚（杀 ntpd 也不行）。含义：**RTC plausible 后，sys 几乎总是 plausible**。于是链的 step1（sys plausible→return "system"）在内核同步后几乎总命中，step2（显式读 RTC）主要只在冷启那扇窗有效。**链设计仍正确**（兜底语义），但实际 `source` 多为 `system`（其值来自 RTC 经内核同步）。

4. **`busybox ntpd` 是常驻守护**：`ntpSyncAndWait` spawn 后 ntpd 不退出，持续同步。含义：wm 里 NTP 调一次后 ntpd 常驻；后续 settimeofday 会被它回滚。wm 关机 poweroff 前无所谓，但**实现时勿依赖「settimeofday 后立即读 sys 反映我设的值」**。

5. **RTC-drift 边界（提请决策，未改 spec）**：因发现 3，若 RTC plausible-但-漂移（如电池没电 RTC 停在某个旧 plausible 日期），内核会把 sys 同步成那个错值，链 step1 直接信它（`source=system`），**wm 启动不会 NTP 纠正**→本次 capture 文件名用错时间；只有关机 writeback 才 NTP 纠正并写 MCU。当前 spec §6.1 step1 就是「sys plausible 即停」。**是否要「启动时无论 sys 是否 plausible 都尝试一次 NTP 纠正 RTC 漂移」是一个待定 spec 取舍**——按治理规则先记、不改 spec，等用户定。

## 过程踩坑（已沉淀）

- `--inject-implausible`（settimeofday 1970）在本机**不可靠**（内核 sys←RTC 回滚）→ 不能用它模拟冷启。冷启 RTC 兜底改为**手动验证**（见上表「手动」行），自动化 case 移除（`test_time_chain.py` 顶部 NOTE 说明原因）。
- `devctl reboot` 是**软重启**，触发已知坑「soft-reboot breaks WiFi」→ NFS 断 → case 跑不了。**但 retry bringup 能恢复**（重新关联 WiFi 即可，非永久损坏）。结论：time_test 系列走**单 boot 多 case**（无 IMP，无需冷启间隔），**不要在 case 里 reboot**。
- CMake：time_test 不碰 IMP，link 比 snap_test 轻（无 imp/alog/hal/media），但需显式 `jsoncpp`（common_misc 的 PRIVATE 依赖不传播）。

## 产物

- `src/app/time_test.cpp`（time_test binary，双平台：`build/bin/` MIPS + `build_sim/bin/` x86）
- `src/app/CMakeLists.txt`（time_test target）
- `tests/host/test_time_chain.py`（7 离线 case，1-boot-1-case 纪律）
- `logs/time_stage1.xml`（junit 报告）

## 下一步

- 决策发现 5（启动是否强制 NTP 纠正 RTC 漂移）→ 若要，改 spec §6.1 + time_test 加 case。
- Stage 2（在线 NTP 专项）：基本已被发现 1 覆盖；可加 `chain-online`（RTC/MCU 都不可信→NTP）case 收尾，但风险已很低。
- 时间链代码 lift 进 wm 启动路径，开始建 wm（调度器 + Capture/Upload lane）。
