# Research: db / json 文件目录 + upload 后处理逻辑

> Topic: `db-json-upload-postprocess`
> Status: done (read-only research)
> Date: 2026-07-01
> Scope: SQLite db 文件位置 / Manifest-json 文件位置 / 文件 upload 成功后的本地处理（删除 vs 移动 vs 不动）/ db 与 json 是否随媒体一起上传
> Codebase root: `/home/zengping/project/t32/code/t32_app`

---

## 1. SQLite / db 文件目录

### 1.1 目录与文件名

`DatabaseManager` 打开 **两个** SQLite 库（`src/storage/DatabaseManager.cpp:61-62`）：

- `<storageDir>/media_file.db` — 媒体元数据（小）
- `<storageDir>/media_thumb.db` — 缩略图 BLOB（大）

代码：

```cpp
// src/storage/DatabaseManager.cpp:61-62
std::string mediaDbPath = m_storageDir + "/media_file.db";
std::string thumbDbPath = m_storageDir + "/media_thumb.db";
```

### 1.2 路径来源：**配置注入，非硬编码**

`DatabaseManager::init(storageDir)` 接收外部传入的目录（`DatabaseManager.h:23`、`.cpp:43-49`）。各进程在 startup 阶段把 `cfg.dbPath` 传进去（`ProcessLifecycle.cpp:293`）。`cfg.dbPath` 按进程 / 平台不同（绝对路径）：

| 进程 | 硬件 (T32) | 模拟 (SIM) | 证据 |
|------|-----------|-----------|------|
| `htc_main_app` (um) | `/mnt/sdcard/data/db`（env `DB_PATH` 可覆盖） | `<simRoot>/data/db` | `src/app/um_app.cpp:122,129` |
| `htc_main_app` (main) | `/mnt/sdcard/data/db` | `<simRoot>/data/db` | `src/app/main_app.cpp:167,176` |
| `htc_workmode_app` | `/mnt/sdcard/data/db` | `<simRoot>/data/db` | `src/app/workmode_app.cpp:85,94` |
| `wm` (新独立 binary) | **`/mnt/huntcam/data/db`** | `<simRoot>/data/db` | `src/app/workmode/wm_paths.h:8,26-32`；`wm_app.cpp:142` |

- 硬件 T32 的 `DB_PATH` env 默认值 `/mnt/sdcard/data/db`（SD 卡）。
- **注意**：`wm` 进程的 db 路径硬编码在 `wm_paths.h:8` 为 `/mnt/huntcam/data/db`（NFS 共享盘 `build/`），与 um/workmode_app 的 `/mnt/sdcard/data/db`（SD 卡）**不同** —— 这是 wm 重构后的有意分离。
- `CameraServiceT32.cpp:542,546` 也单独回退到 `/mnt/sdcard/data/db/media_file.db` / `media_thumb.db`（同一路径常量的另一处定义）。
- http_api_v1 有 lazy init 路径（`http_api_v1.cpp:359`）。

### 1.3 db 里 MetadataDao 存什么

表 `media_files`（`DatabaseManager.cpp:151-173`）每行 = 一个媒体文件，列：

`file_path` (UNIQUE), `type` (1=Photo/2=Video), `timestamp`, `file_size`, `duration`, `width`, `height`, `is_favorite`, `is_locked`, `container_type`, `playback_capable`, `playback_reason`, `playback_token`, `range_supported`, `seek_support`, `seek_granularity_ms`, `effective_gop_frames`, `effective_gop_ms`, `fragment_index_path`。

索引：`idx_media_time`、`idx_media_type`、`idx_media_type_time`、`idx_media_playback`、`idx_media_playback_token`。

表 `thumbnails`（`DatabaseManager.cpp:185-189`）：`(file_path PRIMARY KEY, data BLOB)`。

**MetadataDao 不存"上传状态"。** `F_UploadedTag` 不在 SQLite 里 —— 它在 json（见 §2）。

---

## 2. Manifest / json 文件目录

### 2.1 角色：desc（description）清单 = 上传清单

`manifest::generateDescInfo` / `createDescInfoFile`（`src/manifest/Manifest.cpp:68,253`）把一批媒体文件序列化成 5 段 JSON：

- `F_UploadedTag` — desc 自身的上传标记（0 未传 / 1 已传），`Manifest.cpp:84`
- `file_inf[]` — 每个媒体文件一项，含 `F_FilePath`、`F_FileName`、`F_FileTime`、`F_UploadedTag`（0/1）、`F_CheckCode`（CRC16），`Manifest.cpp:98-109`
- `device{}` — 设备快照（PID、IP、GPS、电池、盘容量、温度…），`Manifest.cpp:114-169`
- `data{}` — 传感器读数（温湿度/气压/433MHz…），`Manifest.cpp:172-200`
- `network{}`、`signal{}` — 网络与无线信号，`Manifest.cpp:202-245`

→ json 同时是 **媒体清单 + 上传任务单元**：上传 worker 按 desc 里的 `file_inf[]` 逐个上传媒体。

### 2.2 目录与命名

desc 落盘目录 = `wmUploadPath()`（`wm_paths.h:46-48`）：

- 硬件 T32：`/mnt/huntcam/media/upload/`（`wm_paths.h:11`）
- 模拟 SIM：`<wmSimRoot>/media/upload/`

命名规则：`<媒体文件名 stem>.json`，与媒体同名去后缀。

- 录影：`record_task.cpp:137` → `wmUploadPath() + fileStem(record_path) + ".json"`（如 `20260701_120000.json` 对应 `20260701_120000.mp4`）
- 拍照：`snap_task.cpp:145` → `wmUploadPath() + fileStem(files[0]) + ".json"`

媒体本身（mp4/jpg）落在 `wmMediaPath()` = `/mnt/huntcam/media/`（`wm_paths.h:9,42-44`），与 desc 的 `upload/` 子目录分开。

### 2.3 谁写 / 谁读

- **写**：`RecordTask::onCompleteRecord`（`record_task.cpp:136-138`）、`SnapTask::trigger`（`snap_task.cpp:144-146`）。录影/拍照成功完成后写 desc 到 upload 目录。
- **读**：
  - `UploadTask::scanAndUploadOnePass` 扫描 upload 目录所有 .json（`upload_task.cpp:196-219`），对每个 desc 调 `uploadOneDesc`。
  - `UploadWorker`（legacy）从内存队列消费（`upload_worker.cpp:80-89`）。
  - `hasPendingWork` 读 desc 判断有无 `F_UploadedTag==0`（`upload_task.cpp:180-194`）。

---

## 3. （重点）文件 upload 成功后的处理逻辑

### 3.1 三代上传实现，同一套行为

| 代次 | 文件 | 服务进程 | 媒体删除策略 |
|------|------|---------|------------|
| legacy | `src/app/workmode/WorkModeRunner.cpp` (~`:843`) | 旧 workmode | 配置门 `FILE_MANAGE_DELETE` 才删 |
| v1 | `src/app/workmode/upload_worker.cpp:270-279` | `htc_workmode_app` / snap | **配置门** `FILE_MANAGE_DELETE` 才删 |
| v2 (当前主路径) | `src/app/workmode/upload_task.cpp:323-348` | `wm` binary | **始终删除**（capture-upload-forget） |

### 3.2 核心机制：`F_UploadedTag` 回写 + 删除（不是移动）

没有任何 **"已上传目录"**。upload 后处理只有两种动作：**原地删除** 或 **原样不动**。从未出现过 `rename` / `move` / `archive` 到另一目录。`Misc::moveFile`（`Misc.cpp:149-160`，调 `mv -f`）在 upload 路径里**一次都没被调用过**（grep 全仓 src，upload 相关文件无 `moveFile` 引用）。

具体流程（以当前主路径 `UploadTask::uploadOneDesc` 为准，`upload_task.cpp:222-349`）：

**Step A — desc 自身上传（`upload_task.cpp:265-290`）**：
1. 若 `F_UploadedTag==0`：`storage->uploadFile(desc_filename)` 上传 desc 这个 .json 文件本身（`:273`）。
2. 等 ≤8s 等 server ack。ack 成功 → 回写 `root["F_UploadedTag"] = 1` 并 `ofstream` 覆写 desc 文件（`:279-282`）。
3. 超时无 ack → `F_UploadedTag` 保持 0，继续往下传媒体（见 NOTE 注释 `:235-240`）。

**Step B — 媒体文件逐个上传（`upload_task.cpp:294-333`）**：
1. 绑定回调：成功就 push 到 `uploaded_file_list`，失败置 `allFileUploaded=false`（`:298-305`）。
2. 遍历 `file_inf[]`，每个 `F_UploadedTag==0` 的项 `storage->uploadFile(pathname)`（`:306-316`）。
3. `while (!storage->isUploadFinished())` 等所有上传完成（`:319-321`）。
4. **对每个上传成功的文件**（`uploaded_file_list`）：
   - 回写 `root["file_inf"][i]["F_UploadedTag"] = 1`（`:327`）
   - **`Misc::deleteFile(pathname)` 删除本地媒体**（`:330`）—— v2 始终删，无配置门（注释 `:328-329` 明示：wm 不再走 FileManage 配置门，capture-upload-forget）

**Step C — 整体收尾（`upload_task.cpp:335-348`）**：
1. 覆写 desc 文件（带上更新后的所有 `F_UploadedTag`）（`:335-337`）。
2. 若 **desc 自身 + 所有媒体全部成功** → **`Misc::deleteFile(desc_filename)` 删除 desc**（`:344-348`）。注释 `:341-343`：wm 不在 SD 留存已传 desc，防 upload 目录无界增长。
3. 若部分成功 → desc 保留（`F_UploadedTag` 已记录哪些传过），下次扫描按未传项重传。

**异常分支也会删 desc**（`upload_task.cpp:236-262`）：非 json / 解析失败 / PID 不匹配 / 无 `file_inf` 字段 → 直接 `Misc::deleteFile(desc_filename)` 丢弃坏 desc。

### 3.3 v1 (UploadWorker) 与 v2 的唯一区别：删除门控

`upload_worker.cpp:270-279`：

```cpp
for (auto& filename : uploaded_file_list) {
    ...
    root["file_inf"][i]["F_UploadedTag"] = 1;
    auto file_manage_type = DeviceConfig::getInstance()->get(INI_SECTION_POLICY, INI_KEY_FILE_MANAGE, 0);
    if (file_manage_type == FILE_MANAGE_DELETE) {   // ← 配置门
        Misc::deleteFile(pathname);
    }
}
```

- v1：媒体上传成功后，**只有 `[POLICY] FileManage=1` (FILE_MANAGE_DELETE) 才删**；默认 `FILE_MANAGE_KEEP=0` → 文件**原样留在原地**（`Common.h:85-88`）。
- v1 **不删 desc**（upload_worker.cpp 末尾无 `deleteFile(desc_filename)`）—— desc 永久保留在 upload 目录。
- v2：**始终删媒体**，且 **全部成功后删 desc**。

### 3.4 `Misc::deleteFile` 实现

`src/common/misc/Misc.cpp:177-184`：调 POSIX `remove()`（非 unlink，非 rename）：

```cpp
bool Misc::deleteFile(const std::string& filePath) {
    if (remove(filePath.c_str()) != 0) { ... return false; }
    return true;
}
```

### 3.5 StorageServClient.upload 本身不做任何本地文件处理

`src/network/StorageServClient.cpp:153-259` 的 `upload()`：打开文件 → send socket → 返回 EC_SUCCESS/EC_FAILED。**没有 remove/rename/move/unlink**。删除决策完全在调用方（upload_task/upload_worker）的回调里。

---

## 4. 是否存在"已上传"目录

**不存在。** 全仓 grep `rename|move|archive|uploaded.*dir|\.uploaded|backup_dir`（排除 SQL rename column/table）结果：

- upload 路径里零 `Misc::moveFile` 调用。
- 零 `rename()` / `renameat()` / `sendfile()` 到"已上传"目录。
- 零形如 `/uploaded` / `/uploaded/` / `.uploaded` / `/archive` 的目录常量。

upload 后处理只有两条路：
- **v2 (wm)**：上传成功 → `remove()` 原地删除（媒体 + desc）。失败 / 部分成功 → 原样留在 `upload/` 目录（desc 带 `F_UploadedTag` 标记），下次扫描重传。
- **v1 (workmode_app / legacy)**：上传成功 → 看 `[POLICY] FileManage` 配置。默认 KEEP=0 → **文件原样留在 `wmMediaPath()` 原地**；desc 永久留在 `upload/`。

---

## 5. db 与 json 是否随媒体一起上传

### 5.1 db：**不上传，纯本地**

`media_file.db` / `media_thumb.db` 是本地索引（元数据 + 缩略图），**不在任何 upload 路径里**。grep `upload.*\.db|media_file\.db.*upload` 零命中。`uploadFile()` 的调用只有两类入参：
- desc json 文件（`upload_task.cpp:273`、`upload_worker.cpp:216`）
- 媒体文件 mp4/jpg（`upload_task.cpp:314`、`upload_worker.cpp:261`）

db 作用域仅限本地 HTTP API（timeline / playback / thumbnail，见 `MetadataDao` 的 getTimeline/getMediaByPlaybackToken/getThumbnail），与服务器同步无关。

### 5.2 json (desc)：**上传 json 本身，作为"清单+首包"**

desc json 不是"跟随媒体上传的附属"，而是 **上传协议的第一步** —— 先 `uploadFile(desc_filename)` 把整份清单 + 设备/传感器快照发给 server（`upload_task.cpp:265-273`），server 回 `Status_ID` ack 后，再逐个上传 `file_inf[]` 里的媒体（`:306-316`）。desc 自身有自己的 `F_UploadedTag`（`Manifest.cpp:84`），独立追踪。

**重要**：上传的是 desc json 这一个文件，不是上传 SQLite 或"所有 json"。upload 目录下只有 desc 类型的 json，没有别的 json。

---

## 6. 目录速查表（wm binary，硬件 T32）

| 角色 | 路径 | 说明 |
|------|------|------|
| 媒体元数据 db | `/mnt/huntcam/data/db/media_file.db` | `wm_paths.h:8`；wm 专用 |
| 缩略图 db | `/mnt/huntcam/data/db/media_thumb.db` | 同上 |
| 媒体文件（mp4/jpg） | `/mnt/huntcam/media/` | `wm_paths.h:9`；录影/拍照产物 |
| desc (upload 清单 json) | `/mnt/huntcam/media/upload/` | `wm_paths.h:11`；命名 `<媒体stem>.json` |
| "已上传"目录 | **不存在** | upload 后原地删除（v2）/ 原地保留（v1） |

对照（um / workmode_app，硬件 T32）：

| 角色 | 路径 |
|------|------|
| db | `/mnt/sdcard/data/db/media_file.db` + `media_thumb.db` |
| 媒体 | `/mnt/sdcard/DCIM`（um）/ `MEDIA_UPLOAD_PATH`（legacy workmode） |
| desc | legacy `MEDIA_UPLOAD_PATH` |

---

## 7. Open questions

- **无**。所有 5 个问题都在代码中找到确切答案。v1/v2 删除策略差异已由 `upload_task.cpp:328-329,341-343` 的注释明确解释为"v2 有意取消配置门、capture-upload-forget"。

---

## 8. Conclusion

1. **db** = `<dbPath>/media_file.db` + `media_thumb.db`；`dbPath` 由进程 startup 注入（um/workmode=`/mnt/sdcard/data/db`，wm=`/mnt/huntcam/data/db`）。MetadataDao 存本地媒体索引（路径/类型/时间/尺寸/播放能力/缩略图），**不含上传状态**。
2. **json** = desc 上传清单，落 `<mediaRoot>/upload/<stem>.json`（wm：`/mnt/huntcam/media/upload/`），由 record/snap task 写，由 UploadTask/UploadWorker 读。5 段结构（F_UploadedTag / file_inf / device / data / network / signal）。
3. **upload 后处理 = 原地 `remove()` 删除，无"已上传"目录、无 rename/move**：
   - 当前主路径 **wm (UploadTask)**：媒体上传成功 → 立即删本地媒体；desc 自身+全部媒体都成功 → 删 desc（capture-upload-forget）。
   - **workmode_app / legacy (UploadWorker)**：媒体上传成功 → 仅当 `[POLICY] FileManage=1 (DELETE)` 才删；默认 KEEP → 留原地。desc 永不删。
4. **不存在"已上传"目录**。失败/部分成功 → 文件（含 desc）原样留在 `upload/`，靠 `F_UploadedTag` 断点续传。
5. **db 纯本地，从不被上传**；json (desc) 本身是上传协议的第一步（先传清单给 server），但只传 desc 这一个文件，SQLite 与"所有 json"都不上传。

**风险提示**：v1 与 v2 删除策略不一致 —— 同一台设备若混跑 workmode_app（KEEP 默认留 SD）与 wm（强制删），媒体留存行为不同。当前主线是 wm（capture-upload-forget），workmode_app 为 legacy。
