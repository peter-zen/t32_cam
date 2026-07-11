# m2 工作单元上传设计（quickSnap ↔ wm 衔接）

> **状态**：已拍板（2026-07-10 grill），待实施
> **取代**：[`wm-app-spec.md`](../knowledge/specs/wm-app-spec.md) §2.1 中「ingest 读 `/tmp/media/info.json` → 造 desc 到 `/tmp` → UploadTask 扫 `/tmp`」一节（spec line 65-66）及 §0 line 16 的 handoff 描述（`wm -m 2` 无目录参数）。
> **关联**：[`wm-app-spec.md`](../knowledge/specs/wm-app-spec.md)（上位规格）、[`upload-protocol-spec.md`](../knowledge/specs/upload-protocol-spec.md) §5（desc 先于媒体）、[`media-output-paths-naming.md`](../research/media-output-paths-naming.md)、[`quicksnap-app-spec.md`](../knowledge/specs/quicksnap-app-spec.md)
> **范围**：仅 m2（quickSnap → wm 上传）链；**m1 / SD 老扫描路径不动**。

---

## 1. 背景与动机

现状（grill 前的问题）：

1. **布局分散**：JPG 在 `/tmp/media/<ts>/`、quickSnap 的 `info.json` 在 `/tmp/media/info.json`（每次覆盖）、wm 造的 desc 在 `/tmp/<ts>.json`。三处分散，清理各管各的，`info.json` 甚至无人删，残留累积。
2. **wm 扫整个 `/tmp/`**：`UploadTask` 把 `/tmp/` 下每个普通文件当 JSON 解析、当 desc 候选。`/tmp/` 里任何一个「合法 JSON 但不是 object」的杂项文件，都会让 `hasPendingWork` 的非 const `operator[]` 抛 `Json::resolveReference(key, end): requires objectValue`，且该调用在 try/catch 之外 → `std::terminate` 打死 wm。（2026-07-10 实测复现：quickSnap→wm 上传本身成功，却在「上传后重扫 `/tmp/`」时崩溃。）
3. **handoff 无目录参数**：`wm -m 2` 不知道本次该传哪个目录，只能盲扫 `/tmp/`。

本设计把三件事（布局 / 传参 / 扫描）理顺为一条「**自包含工作单元**」契约。

## 2. 目标

- quickSnap 产出（媒体）与 wm 产出（desc）收进同一个 `/tmp/media/<ts>/` 目录。
- quickSnap 把目录显式传给 wm；wm 只扫该目录（+ 可选兜底扫描其它滞留目录），不再扫 `/tmp/`。
- 从结构上消除「`/tmp/` 杂项文件打死 wm」的崩溃面（外加防御性补丁兜底）。
- 支持同会话内崩溃后**断点续传**（desc 持久化 + `F_UploadedTag`）。

## 3. 工作单元目录布局

**工作单元 = 一个自包含目录**：`/tmp/media/<ts>/`，`<ts>` = `YYYYMMDD_HHMMSS`（quickSnap 现有 `getCurrentTimeFormatted()` 格式，正则 `^\d{8}_\d{6}$`）。

| 文件 | 写入方 | 说明 |
|---|---|---|
| `<ts>_N.<ext>`（媒体，N=1..burstNumber） | quickSnap | 拍摄产物；`<ext>` ∈ 白名单（§7） |
| `<ts>.json`（desc） | **wm** | 上传协议 json（`F_UploadedTag` / `device` / `file_inf`）；wm 拥有，断点续传用 |
| ~~`info.json`~~ | ~~quickSnap~~ | **删除**。wm 改为扫目录白名单媒体来建 desc；info.json 既冗余又干扰扫描 |

**变更对照**：

| | 之前 | 之后 |
|---|---|---|
| 媒体 | `/tmp/media/<ts>/<ts>_N.JPG` | 同左（不变） |
| quickSnap manifest | `/tmp/media/info.json`（每次覆盖） | **删除** |
| desc | `/tmp/<ts>.json` | `/tmp/media/<ts>/<ts>.json`（移入工作目录） |
| UploadTask 扫描目录 | `/tmp/`（全部普通文件） | 传入的工作目录（仅该目录内） |

## 4. quickSnap 职责

1. 拍照，媒体写入 `/tmp/media/<ts>/`（今天只有 JPG；视频经由 `record_task` 另一条路径，不经 quickSnap）。
2. **不再写 `info.json`**（删除 `src/app/quick_snap.cpp:220-244` 的 manifest 落盘）。
3. spawn 时把**绝对路径**目录传给 wm：
   ```c
   // src/app/quick_snap.cpp:401-412 的 wmArgv 增加 -d
   char* const wmArgv[] = {
       const_cast<char*>("wm"),
       const_cast<char*>("-m"),
       const_cast<char*>("2"),
       const_cast<char*>("-d"),
       const_cast<char*>(absDir.c_str()),   // /tmp/media/<ts>
       nullptr
   };
   spawn(wmPath, wmArgv);
   ```
   传绝对路径，避免 wm CWD 歧义。仅当 `willSnap && !snapDir.empty()` 时带 `-d`（force_upload 路径 `willSnap=false` → 不带，靠兜底）。
   - ✅ **rtc-fail 命名已统一**（2026-07-11）：rtc 同步失败时工作目录**不再**叫 `/tmp/media/pic/`，而是**始终**用当前系统时间命名 `/tmp/media/<YYYYMMDD_HHMMSS>/`（时间不可信也照用，见 `quicksnap-app-spec §5.1`）。历史背景：旧 "pic" 非时间戳名不匹配兜底正则 `^\d{8}_\d{6}$`，本进程 `-d` 显式入队可传，但 T22「上传失败 SD 落卡」后 `persistStrandedTmpDir` 会把 "pic" 落到 SD，下次 boot SD-resume 的 `isTimestampDir` 认不出 → 永久 stranding。统一时间戳命名从源头消除该 gap。

## 5. wm 职责（m2）

**CLI**：新增 `-d` / `--dir <dir>`，沿用 `src/app/wm_app.cpp:178-194` 的手写解析风格。m2 下 `-d` **可选**（force_upload / `willSnap=false` 时 quickSnap 不带 `-d`，wm 靠兜底扫描补传滞留）。工作目录列表 = [-d 目录（若有）] + [兜底滞留目录]，空 → 无事可传 → idle-grace 关机。

**启动流程**：

1. **主目录**：处理 `-d` 指定的工作目录。
2. **兜底扫描**（开关 ON 时，§6）：扫描 `/tmp/media/` 下其它滞留 `<ts>/`，并入队列。
3. 对队列里**每个工作目录**（升序、老先传）执行「建/载 desc → 上传 → 清理」。

**单个工作目录的处理**（`wm_ingest.cpp` 重写 + `UploadTask` 扫描目录改为工作目录）：

- **desc 不存在** → 按白名单（§7）扫媒体 + 现读 MCU/传感器（`src/manifest/Manifest.cpp:68+` `generateDescInfo`，**行为不变**）→ 建 desc 落盘到 `<ts>/<ts>.json`（`F_UploadedTag` 全 0）。
- **desc 已存在**（上次崩了重跑）→ **直接加载，不重建**（保留 `F_UploadedTag` 进度，避免大视频从头重传）。
- 先传 desc，再按 `file_inf` 传各媒体（仅 `F_UploadedTag==0` 的，传完置 1 落盘）——协议同 [`upload-protocol-spec.md`](../knowledge/specs/upload-protocol-spec.md) §5。
- **全部成功** → `rm -rf <ts>/`（替代现状分散的「删 desc / 逐个删媒体」）。

## 6. 兜底扫描模块（独立、可开关）

| 项 | 契约 |
|---|---|
| **开关** | 环境变量 `HTC_WM_SWEEP_STRANDED=1/0`，**默认开**（=1）。风格同现有 `HTC_TEST_NO_POWEROFF` / `HTC_UPLOAD_DIAG` / `HTC_WM_ONE_SHOT` 等 toggle |
| **形态** | 独立纯收集器：`collectStrandedWorkDirs(baseDir="/tmp/media", excludeDir=<-d目录>) → std::vector<std::string>`。只被 m2 启动调一次，**不碰上传逻辑**。开关 = 「调不调它」 |
| **范围** | `baseDir` 的**直接子目录**，名字匹配 `^\d{8}_\d{6}$` 才算工作单元 |
| **排除** | 跳过 `-d` 目录，避免重复处理 |
| **顺序** | 与主目录合并后按目录名升序（老先传） |
| **可观测** | 命中时打一行 `[wm] sweep: found N stranded dir(s): ...` |

设计要点：主目录处理**永远跑**（不依赖兜底）；兜底是独立的增量能力，关掉即退化为「只处理 `-d` 目录」的单目录确定性模式（便于调试）。

## 7. 媒体识别白名单

wm 扫工作目录建 desc 时，只有扩展名命中白名单的文件进 `file_inf`。起步集合：

```
{ ".jpg", ".jpeg", ".mp4" }
```

- desc 本身（`<ts>.json`，wm 已知路径）走独立通道、总第一个上传，**不参与**白名单规则。
- 白名单单点维护；后续新增媒体类型（如单独上传 `.aac`、新格式）只改这一处。
- 当前事实：quickSnap 只往目录写 JPG（`withThumb=false`，不建缩略图）。白名单今天只需覆盖图片，但视频录制已存在（`record_task` 路径），故 mp4 一并纳入起步集合。

## 8. 失败与清理语义（capture-upload-forget，承接 wm-app-spec §5.2 line 178）

- **逐文件续传**：desc 持久化在 `<ts>/`，每个文件 `F_UploadedTag` 记进度；传成功的媒体**即时删除**（省空间，已验证路径）。
- **部分失败**：desc（带进度）+ 未传媒体留在 `<ts>/`，目录不删；下一次兜底扫描（同会话内 wm 再跑）按 `F_UploadedTag` **只补未传的**，不重传已传的。
- **全部成功**：`rm -rf <ts>/`（desc + 可能残留一次性清掉）。
- **不设「放弃」条件**：媒体一直传不上就跨 sweep 重试，直到成功或会话结束。网络故障是瞬时的会重连；「判定永久失败并删掉」会主动丢照片，对轨迹相机不可接受。掉电后 `/tmp`（tmpfs）失是另一回事——已尽力传过，不算静默丢失。

## 9. desc 文件细节

- **路径**：`/tmp/media/<ts>/<ts>.json`（移入工作目录）。
- **命名**：`<ts>.json`（**目录级**，一个 desc 覆盖整个工作目录的全部媒体）。注意这与 m0/m1 CaptureTask 的「每媒体一个 desc、`<ts>_N.json`」（wm-app-spec §5.2 line 176）不同——m2 是目录级聚合 desc，二者各自独立。
- **服务端**：desc→媒体的服务端关联仍靠 `file_inf.F_FileName`（确切名）；`F_FilePath`=`/tmp/media/<ts>`、desc 的 `FileName`=`<ts>.json`，**均与现状一致，服务端协议无需改动**。

## 10. 防御性修复（必带，独立于本设计）

新布局让工作目录变干净（只有媒体 + desc），从结构上消除崩溃触发面，但 `UploadTask` 仍会在每个工作目录内扫 desc，故仍必须打两个补丁（即 2026-07-10 调试的直接产物）：

- `src/app/workmode/upload_task.cpp:180-194` `hasPendingWork`：`reader.parse` 后加 `if (!root.isObject()) return false;`（非对象 JSON 不当 desc）。
- `src/app/workmode/upload_task.cpp:204-211` `scanAndUploadOnePass`：把 `hasPendingWork(p)` 调用纳入既有 try/catch，异常打日志而非 `terminate`。

## 11. 与 wm-app-spec §2.1 的差异（取代表）

| wm-app-spec §2.1 现描述 | 本设计 |
|---|---|
| handoff = `wm -m 2`（无目录） | `wm -m 2 -d /tmp/media/<ts>` |
| ingest 读 `/tmp/media/info.json` 建 desc | **删除 info.json**；wm 扫 `-d` 目录白名单媒体建 desc |
| desc 写到 UploadTask 扫描目录 `/tmp` | desc 写进工作目录 `/tmp/media/<ts>/<ts>.json` |
| UploadTask 扫 `/tmp` | UploadTask 扫传入工作目录（+ 可选兜底扫描其它 `<ts>/`） |

> wm-app-spec §2.1 line 65-66 与 §0 line 16 的对应文字，在实施合入后需同步更新为本设计。

## 12. 实施改动点

- `src/app/quick_snap.cpp:220-244` — 删 `info.json` 写盘；`:401-412` — `wmArgv` 加 `-d <abs_dir>`。
- `src/app/wm_app.cpp:176-194` — 解析 `-d`/`--dir`；`:350-353` — ingest 改为「主目录 + 兜底收集器」组合。
- `src/app/workmode/wm_ingest.cpp:19-63` — 重写 `ingestQuickSnapManifest`：扫工作目录白名单媒体 → 建 desc 到 `<ts>/<ts>.json`；desc 已在则跳过新建（续传）。
- `src/app/workmode/wm_scheduler.cpp:214-224` — `UploadTask` 的 `uploadDir_` = 工作目录（不再 `/tmp/`）；外层按「工作目录列表（主 + 兜底）」循环处理。
- 新增独立模块 `collectStrandedWorkDirs()` + `HTC_WM_SWEEP_STRANDED` 开关。
- `src/app/workmode/upload_task.cpp:180-194 / 204-211` — 防御性补丁（§10）。
- **不动**：m1/SD 扫描路径、`Manifest.cpp` 传感器读取、服务端协议、m0/m1 CaptureTask desc 命名。

## 13. 不做 / 后续

- 拍摄时刻传感器快照（当前为上传时刻现读）：如有需要再引入一个极简 manifest，本次不做。
- quickSnap 直产视频：目前视频走独立 `record_task` 路径，不经 quickSnap；若以后统一，再扩展。
- 跨硬重启续传：`/tmp` 为 tmpfs，掉电即失；如需跨重启保留，需把工作目录落到 SD（单独议题）。
- m1/SD 上传路径与本设计的对齐（保持现状，不在本阶段）。
