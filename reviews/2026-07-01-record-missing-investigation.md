# 连续录影第 2 段在历史回放里消失 — 根因定位

**日期**：2026-07-01
**作者**：zengping（+ Claude 协作）
**状态**：根因已由封口日志**直接观察确认**（`rc=-1` + 首帧 P 帧 `hdr=...41`）；健壮性修复已落地（`requestIDR()` + drain 到首 IDR），待设备复现验证
**入口日志**：`build/logs/app.log`（两次复现：09:27 段 + 10:35 段）

## 症状

`um` 服务态：APP 端做 1 次拍照 + 2 次录影，进历史回放只看到前两个文件，**第 2 段录影看不到**。

## 根因（一句话）

第 2 段录影第一帧的第一个 NAL 写 MP4 时被 muxer 拒收（`mp4_h26x_write_nal` 因 `need_sps` 返回 `MP4E_STATUS_BAD_ARGUMENTS`），触发 `VideoRecorder` 的**静默 abort 路径**（`stream_->stop()` + `return false`），该路径在"写媒体 DB"之前返回 → 文件不入库 → 历史里查不到。第 1 段录影正常（首帧带 SPS/PPS）。

## 证据链（10:35 复现，带诊断打桩）

`um --no-audio --force-day` 启动（`app.log:7` `no_audio=1`），全程无 insmod，**问题与音频无关，纯连续录影路径**。

| 事件 | 时间 | 证据 |
|------|------|------|
| 录影 1 正常 | 10:35:16–23 | worker tid=1100；手动 stop；`Saved video to DB` ✓（`app.log:295`）；size=3126182 manual=1 |
| 旧 recorder 析构（录影 2 的 startRecord 清理） | 10:35:30.848 | `[VR-DIAG] deinitialize/uninitVideo entry tid=1090`；`ch0 stop caller_tid=1090`（HTTP 线程，非 worker） |
| 录影 2 启动 | 10:35:31.942 | `[VR-DIAG] record worker tid=1107`（`app.log:415`） |
| 录影 2 进循环 | 10:35:32.020 | `TRACE [4/4] entering record loop`（`app.log:434`）；CH2 缩略图已停 |
| **worker 自停 ch0** | 10:35:32.027 | `[HAL-DIAG] ch0 stop caller_tid=1107 ref=1 started=1`（`app.log:437`）—— **caller_tid==record worker，非外部线程** |
| 录影 2 失败 | 10:35:32.152 | `record done size=24 manual=0`（`app.log:443`）；**无 `Saved video to DB`** |

**关键否定**：10:35:32.027 停 ch0 的是 worker 自己（tid 1107），不是析构线程（1090，早在 30.848 已析构完）。我先前"外部线程在录影途中拆 ch0"的假设被 TID 打桩**推翻**。

## 代码路径定位

`VideoRecorder::record` 循环里，**唯一**会在录影中途 `stream_->stop()` 的早退路径是 `src/media/video/VideoRecorder.cpp:726-732`：

```cpp
if (MP4E_STATUS_OK != mp4_h26x_write_nal(&mp4wr, inputData + pos, (int)nal_size, nal_duration)) {
    stream_->releaseFrame(frame);
    stream_->stop();          // :728 ← 32.027 的 ch0 stop 就是这里
    MP4E_close(muxer);
    mp4_h26x_write_close(&mp4wr);
    fclose(fp);
    return false;             // :732 ← 跳过 :1042 的 dao.addMedia → 不入库
}
```

这条路径**无任何日志**，且 `return false` 在 DB 写入（`:1042`）之前——完美匹配观察到的 5 个事实：① worker 自停 ch0；② size=24（0 帧写入）；③ manual=0（非用户停）；④ 无 `Saved video to DB`；⑤ 无 ERROR 日志。

## 为什么 `mp4_h26x_write_nal` 失败（minimp4）

`MINIMP4_TRANSCODE_SPS_ID=1`（`minimp4.h:35`）→ 生效分支 `minimp4.c:1841-1920`：

- `case 5`(IDR) / `default`(任意 VCL NAL)：`if (h->need_sps) goto exit_with_free → return MP4E_STATUS_BAD_ARGUMENTS`（`minimp4.c:1892-1899` / `1875`）。
- `need_sps` 初值 = 1（`minimp4.c:1761`）。

⇒ **muxer 要求流里先来 SPS(type 7)；首帧若直接是 IDR/slice 而无前置 SPS，写 NAL 立刻失败。**

录影 1（开机后冷编码器）首帧带 SPS/PPS，正常；录影 2 在编码器通道 `DestroyChn`+`CreateChn` 重建后，首帧进循环仅 6ms（30fps 冷启动理应 ≥33ms）就被 poll 出来——**疑似重建后编码器首发的访问单元不是规范 SPS+PPS+IDR**（残留帧 / IDR 未带 inline SPS·PPS），命中 `need_sps` 拒收。

属已记录的"同 boot 内连续录影 IMP/编码器残留"缺陷族——见 `doc/knowledge/bugs/T32-imp-residue-workmode-record-2026-06-21.md`（那份是 `CreateChn(0) failed`，本例是 CreateChn 成功但首帧被 muxer 拒，同一根因不同表现）。

## 本次附带改动

**① 诊断打桩（临时，定位完应移除）**
- `src/hal/ingenic/IngenicVideo.cpp`：`IngenicVideoStream::stop()` 对 ch0 打 `caller_tid`（`[HAL-DIAG]`，INFO 级）。
- `src/media/video/VideoRecorder.cpp`：record-worker / `deinitialize` / `uninitVideo` entry 各打 tid（`[VR-DIAG]`）。

**② 音频清理（用户决策：方案 A）** —— 应用层不再 `insmod` 加载音频驱动；无音频硬件用 `--no-audio` 显式关。
- `src/service/camera/impl/CameraServiceT32.cpp`：删除 `startRecord` 里的 `lsmod/insmod` 探测块；`effectiveAudio = audio && !audioDisabled` 保留。
- `src/app/um_app.cpp`：`UmCliFlags` 加 `noAudio`；`parseFlags` 认 `--no-audio`；`main` 里 `setenv("HTC_NO_AUDIO","1")`（对齐 `-m/--mobile`）；flags 日志加 `no_audio=%d`。

**设备校验 md5**（`/mnt/huntcam/`）：
```
ece87fe3f251c54da9383a110d441307  bin/um
c08a1ea34b9f7028e0b021fe0670ff1b  lib/libapp_workmode.so
60b91e9fbb8d243cce9a225f6c1f5e07  lib/libmedia_recorder.so   ← 含封口日志 + requestIDR/drain 修复
00e2347c0f481deb87879c12123a79ff  lib/libhal_video.so
```

## 封口日志直接确认（第 3 次复现，`app.log:447`）

```
[VR-DIAG] mp4_h26x_write_nal FAIL rc=-1 frame#=1 piece=0/1 nal_size=26298 hdr=00 00 00 01 41 e0 c0 88
```
- `rc=-1` = MP4E_STATUS_BAD_ARGUMENTS ✓
- `frame#=1`，首帧即拒 ✓
- `nal_size=26298`（整个 slice，非 SPS/PPS）✓
- `hdr=00 00 00 01 41`：start code 后 NAL header `0x41`，`0x41 & 0x1f = 1` = **非 IDR slice（P 帧）** → muxer `need_sps` 拒收 ✓

录影 2 重建编码器通道后首帧是无 SPS/PPS 的 P 帧（worker 进循环 ~5ms 即拿到，疑似重建期首发 slice），根因彻底坐实。

## 修复（已落地）

`src/media/video/VideoRecorder.cpp`：
1. `stream_->start()` 后调 `stream_->requestIDR()`——强制编码器首发带 SPS/PPS 的 IDR。
2. record 循环 getFrame 后，**丢掉开头的非 IDR 帧**（`!frame.key` → releaseFrame + continue），拿到第一个 keyframe 再开写 mp4；上限 120 帧（~4s）防死循环。
3. `:735` 封口日志保留（回归观测用）。

修复后预期：录影 2 不再 abort，日志见 `record: first IDR keyframe after draining N non-key frames` + `Saved video to DB`。

## 修复方向（参考 / 后续）

1. ~~封口日志~~ ✅ 已确认。
2. ~~健壮性主修（requestIDR + drain）~~ ✅ 已落地，待验证。
3. **预热 SPS/PPS**：录影开始时从编码器取 SPS/PPS 先喂 muxer（`MP4E_set_sps/pps`），与 RTSP 预览 `Preopen extracted SPS/PPS` 同源（备选/加固）。
4. **根治（重）**：连续录影间不做整段 `DestroyChn`+`CreateChn`，复用已 configure 的编码器通道（`phase1-module-stabilization`）。

## 关联

- 同族缺陷：`doc/knowledge/bugs/T32-imp-residue-workmode-record-2026-06-21.md`
- 诊断前提：最近 commit `5da4c1a feat(elog): async drop-oldest ring`（异步 elog 在并发突发时会丢日志，正是首次复现里 ERROR 日志缺失的原因）
