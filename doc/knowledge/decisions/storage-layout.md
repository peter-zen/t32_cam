# um / wm 媒体存储与上传目录布局决策

## 1. 决策主题

为 `um`（长驻交互服务）与 `wm`（一次性拍/传任务）两个应用程序统一**产出文件的目录管理架构**，覆盖五类对象：照片/视频、缩略图、db 文件、json（desc）文件、upload 后的本地处置与空间回收。

目标：消灭当前三套并存、互不一致的目录规则，建立一棵**根可注入、命名统一、状态驱动**的目录树，并把路径管理集中到单一入口。

## 2. 背景

`um` 与 `wm` 是两个独立 binary（`src/app/CMakeLists.txt:65-73`），加上尚未退役的 `htc_main_app -wm`（legacy），实际跑着**三套目录规则**——命名前缀、扩展名大小写、根介质、burst 序号全不统一；路径定义散落 5 处，无统一 PathManager；upload 后直接 `remove()` 无归档；db 存绝对路径、介质一变索引即失效。

本决策在 S1（抽 `StoragePaths`）落地前定型，是成本最低的窗口。现状细节见三份调研：

- `doc/research/um-wm-identity-and-paths.md` — um/wm 身份与路径根目录体系
- `doc/research/media-output-paths-naming.md` — 照片/视频/缩略图目录与命名
- `doc/research/db-json-upload-postprocess.md` — db/json 路径与 upload 后处理

## 3. 事实基础：当前三套并存

| 文件类型 | `um`（新） | `wm`（新） | `htc_main_app -wm`（legacy） |
|---|---|---|---|
| 照片 | `/mnt/sdcard/DCIM/IMG_<ts>.jpg` | `/mnt/huntcam/media/<ts>_<idx>.jpg` | `/mnt/sdcard/media/<sess-ts>/<ts>_<idx>.JPG` |
| 视频 | `/mnt/sdcard/DCIM/VID_<ts>.mp4` | `/mnt/huntcam/media/<ts>.mp4` | `/mnt/sdcard/media/<ts>.mp4` |
| 缩略图 | DB BLOB | DB BLOB | DB BLOB |
| db | `/mnt/sdcard/data/db/` | `/mnt/huntcam/data/db/` | `/mnt/sdcard/data/db/` |
| json(desc) | — | `/mnt/huntcam/media/upload/<stem>.json` | — |
| upload 后 | — | 原地 `remove()`（媒体+desc） | 仅 `FILE_MANAGE=1` 删媒体 |

关键证据：`CameraServiceT32.cpp:194,430`（um 硬编码 DCIM）、`wm_paths.h:8-48`（wm 常量）、`app.h:20,26`（legacy 宏）、`upload_task.cpp:323-348`（无脑 remove）、`MetadataDao.cpp:93-114`（缩略图 BLOB）、`CameraRecorder.cpp:41-52`（死字段 `thumbnailPath`）。

主要痛点：

1. **介质割裂**：um 写 SD、wm 写 NFS，两套 db 两棵树，um 历史回放看不到 wm 产物。
2. **命名三套**：前缀 / 扩展名大小写 / burst 序号全不同；um 同秒 burst 撞名（时间戳仅到秒、无序号兜底）。
3. **DCIM 扁平**：文件堆积不分桶，但无 db 索引辅助清理/上传分批。
4. **缩略图四概念**：真相是 DB BLOB，但 `RecordResult::thumbnailPath` 是死字段、`MediaScanner` 扫 `thumb_pending/*.thumb.jpg`、`snap_test` 写 `<jpeg>.thumb.jpg`——只有一个是真的。
5. **upload 后直删**：无归档，不可追溯/重传。
6. **db 存绝对路径**：`/mnt/sdcard/DCIM/...` 写死，介质变化索引失效。
7. **路径定义散落 5 处**：`wm_paths.h`、`app.h:20,26`、`Common.h:83`、`StartupConfig`（`ProcessLifecycle.h:19-36`）、`CameraServiceT32` 硬编码。

## 4. 决策结论

### 4.1 根目录可注入

生产 root = `/mnt/sdcard`（SD 卡，持久化）；dev/test root = `/mnt/huntcam`（NFS，方便开发机直接看文件）。`/mnt/huntcam` 定性为 **dev 注入值，非生产路径**——回答了调研中"`/mnt/huntcam` 生产可用性"的 open question。

### 4.2 目标目录树

```
<root>/                         # 注入：/mnt/sdcard（生产）| /mnt/huntcam（dev）
├── DCIM/                        # um 产出，永不上传（扁平）
├── media/                       # wm 产出，进 upload 流程（扁平）
├── data/db/
│   ├── media_file.db            # 合库（um+wm），带 source + status 列
│   └── media_thumb.db           # 合库，缩略图 BLOB
├── upload/                      # 仅 wm
│   ├── ready/                   # 待传 desc json（<stem>.json）
│   ├── done/                    # 冷归档（裸媒体，已出 db，FS LRU 回收）
│   └── failed/                  # 传失败，待重试
├── log/
└── config/
```

### 4.3 媒体目录分离：`DCIM/`（um）vs `media/`（wm）

理由：**策略不同**——wm 文件进 upload 流程，um 拍的永不上传。沿用历史子目录名（wm 本就用 `media/`、um 本就用 `DCIM/`），不嵌套、不重命名，迁移成本最低。两者扁平存放，靠 db 索引（`mtime`/`status`）而非目录分桶支撑清理与上传分批。

### 4.4 命名规范

```
<TYPE>_<YYYYMMDD_HHMMSS>_<NNN>.<ext>
IMG_20260701_120030_001.jpg      # 单拍 = 000，burst 第 N 张 = NNN
VID_20260701_120030.mp4
```

- `TYPE` ∈ {`IMG`,`VID`}；`NNN` 3 位同秒序号（修 `CameraServiceT32.cpp:194,430` 的 um 撞名）；ext 统一小写。
- desc json 与媒体同 stem：`<stem>.json`，跟随媒体在 ready/done/failed 间移动。
- 收敛到一个 `makeMediaName(TYPE, ts, seq)`，um/wm 共用。

### 4.5 数据库：合库 + 相对路径 + WAL

- **合库**：一个 `media_file.db`（um+wm）+ 一个 `media_thumb.db`（缩略图 BLOB），靠 `source` 列与路径前缀（`DCIM/` vs `media/`）区分。分库留待将来（见 §8）。
- **schema**（`media` 表）：`stem`(PK) + `source`(um/wm) + `path`(相对 root) + `type`(IMG/VID) + `status` + `mtime` + `size`。
- **db 存相对路径**（如 `DCIM/IMG_...jpg`），挂载点/介质变化不失效。
- **并发**：um 长驻 + wm 触发可能并发写同一 db → 开 **WAL 模式** + 短事务。

### 4.6 状态机

```
um:  captured（终态，永不上传）
wm:  captured → ready → 上传 → [成功出库 | failed]
```

成功即**出库**，无 `uploaded` 态——因为成功后要么删（出 db）要么移 done/（也出 db），db 只管活跃文件。当前 `MetadataDao.h:10-31` 缺 status 列，需补。

### 4.7 upload 处置：双策略 + 集中点

所有"上传成功后处置"集中在单一函数 `onUploadSuccess(stem, policy)`，`policy ∈ {DELETE, KEEP}`：

| policy | 动作 |
|---|---|
| `DELETE` | `purgeMedia(stem)` 级联清：删 `media/` 媒体 + `ready/` desc + db 记录 + thumb BLOB |
| `KEEP` | 媒体移 `done/`、删 `ready/` desc、删 db 记录 + thumb BLOB（done/ 退出 db） |

替换 `upload_task.cpp:323-348`（无脑 remove）、`upload_worker.cpp:270-279`（`FILE_MANAGE` gate）与 gate 枚举（`Common.h:85-88`）。`FILE_MANAGE` 本质就是早期的 DELETE/KEEP——升级为显式 policy + 完整状态机。**policy 切换只改这一处**。

`purgeMedia(stem)` 是所有"活跃文件删除"的唯一入口（含 failed 清理），级联清四样，杜绝留垃圾。

### 4.8 循环回收（卡满时删旧）

- **时机**：`upload` 完成回调。**不在启动时做**——保证拍照/录影的启动及时性；upload 本就是后台任务尾巴，不挤 capture 路径，且 KEEP 策略刚把文件塞进 done/，是检查水位的自然点。
- **对象**：`upload/done/`，文件系统遍历按 mtime **LRU**（最旧优先）。done/ 文件已出 db（§4.7 KEEP 删了 db 记录），故**回收不查 db**。
- **水位**：按剩余空间百分比，滞回防抖——free% < LOW(10%) 起删，≥ HIGH(20%) 停。
- **动作**：纯 `unlink`（done/ 已无 db 信息，无级联）。

### 4.9 缩略图

DB BLOB（`media_thumb.db`）是唯一真相，保留。收敛掉其余三个概念：

- 删死字段 `RecordResult::thumbnailPath` + `CameraRecorder::computeThumbnailPath`（`CameraRecorder.cpp:41-52`，算路径从不写）。
- `MediaScanner` 的 `thumb_pending/*.thumb.jpg`（`MediaScanner.cpp:229-302`）**用途确认后再定**——疑似为离线扫描外部/历史文件补入库设计，不贸然删。
- `snap_test` 的 `<jpeg>.thumb.jpg` 是诊断输出，保留。

### 4.10 路径集中管理

抽 `StoragePaths`（根注入），um/wm 共用。`wm_paths.h` 是雏形，升级为全 app 共用入口，提供 `root()/dcim()/media()/mediaDb()/thumbDb()/uploadReady()/uploadDone()/uploadFailed()/makeMediaName()/purgeMedia(stem)`。干掉其余 4 处散落定义：`app.h:20,26`、`Common.h:83`、`StartupConfig`、`CameraServiceT32` 硬编码。

## 5. 不在本次范围（open）

- **DCIM（um，永不上传）与 `media/`（wm 未传积压）的清理策略**：§4.8 回收只覆盖 `upload/done/`。若卡满主因是这两处，当前回收救不了，需另定策略（um 的清理是独立话题）。
- **SD 卡文件系统确认**：扁平 DCIM 在 FAT32 下有单目录文件数上限，ext4 则无忧——落地前 `mount | grep mmcblk` 确认。
- **`thumb_pending` 用途确认**：决定 §4.9 是否移除 `MediaScanner` 那条路径。

## 6. 落地阶段

| 阶段 | 内容 | 风险 |
|---|---|---|
| S1 | 抽 `StoragePaths`，um/wm 改用它（行为不变） | 低 |
| S2 | 命名统一 `makeMediaName` + um 加 burst 序号（修撞名） | 低 |
| S3 | db 加 status/source 列 + path 相对化 + 开 WAL | 中 |
| S4 | upload policy 框架（DELETE/KEEP + ready/done/failed + `onUploadSuccess`） | 中 |
| S5 | 循环回收（upload 完成回调触发 + done/ FS LRU + 水位滞回） | 中高 |
| S6 | 清缩略图死字段（先确认 `thumb_pending` 用途） | 低 |

S1/S2 可独立先做、立刻见效；S3 是 S4/S5 的地基。

## 7. 与现有决策/代码的关系

- 与 [`workmode-usermode-process-split.md`](workmode-usermode-process-split.md) **正交**：那份定 um/wm 的**进程边界**（按运行语义），本决策定两进程的**存储布局**。§4.3 媒体目录分离（DCIM vs media）的依据正是那份的运行语义边界——wm 一次性任务会 upload、um 长驻服务不上传。
- 与 [`asymmetric-snap-vs-record-design.md`](asymmetric-snap-vs-record-design.md) 上下层：那份定 snap/record 的采集设计，本决策的命名/目录是其产出文件的容器与生命周期。

## 8. 何时重新评估

- um 出现"需要上传"的诉求（`DCIM/` 也要进 upload 流程）→ §4.3 分离前提动摇。
- 出现"统一相册跨 um/wm 浏览"硬需求 → 重估 db 是否分库（§4.5）。
- 引入狭义"循环录影"（边录边覆盖视频段）需求 → §4.8 的 LRU 水位回收需重设计为段级覆盖。
- um/wm 从互斥部署变为同 boot 共存 → db 并发模型（§4.5 WAL）需复核。

## 9. 相关文档

- 调研：[`doc/research/um-wm-identity-and-paths.md`](../../research/um-wm-identity-and-paths.md)、[`media-output-paths-naming.md`](../../research/media-output-paths-naming.md)、[`db-json-upload-postprocess.md`](../../research/db-json-upload-postprocess.md)
- specs：[`specs/wm-app-spec.md`](../specs/wm-app-spec.md)、[`specs/um-app-spec.md`](../specs/um-app-spec.md)（um/wm 定义）
- 源码：`src/app/workmode/wm_paths.h`、`src/app/app.h`、`src/common/Common.h`、`src/storage/DatabaseManager.cpp`、`src/storage/MetadataDao.h`、`src/app/workmode/upload_task.cpp`、`src/service/camera/impl/CameraServiceT32.cpp`
