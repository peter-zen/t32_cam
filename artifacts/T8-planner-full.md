# T8 — Work Mode Extraction, Phase A (direction + inventory)

> Phase A scope: documentation + inventories ONLY. No `src/`, `CMakeLists.txt`, or `doc/` changes. Those are Phase B/C.
> Every file:line claim below was verified by reading/grepping the file in this worktree (`feature/new-workmode`, `main_app.cpp` is 1894 lines).

---

## 1. Thesis validation (honest, code-grounded)

**User thesis:** extract `-wm` out of the god-binary `htc_main_app` onto a shared **SDK layer** of reusable capability APIs, with work mode / user mode becoming thin app-layer orchestrators.

**Verdict: largely agree, with one critical correction of framing.**

The "SDK layer" the user posits **already exists** as a set of capability libraries. Concretely verified:

| Capability | Lib | Type (from CMakeLists) | Already a clean API? |
|---|---|---|---|
| Network/WiFi/DHCP/NTP | `common_misc` | **SHARED** (`src/common/misc/CMakeLists.txt:20`) | Yes — `Misc::connectWifi/startDHCP/ntpSync` (`Misc.h:31-36`) |
| Photo + thumb | `media_snap` | **SHARED** (`src/media/snap/CMakeLists.txt:21`) | Mostly — `ImageSnap::snap` + `getThumbnailData` (`ImageSnap.h:42-49`) |
| Video record | `media_recorder` + `camera_service` | SHARED + STATIC | Mostly — `CameraRecorder` (`src/service/camera/CameraRecorder.h`) |
| mDNS | `discovery_service` | **STATIC** (`src/service/discovery/CMakeLists.txt:6`) | Yes, low coupling — `MdnsService::start/stop` (`MdnsService.h:29-32`) |
| RTSP server | `media_rtsp` | **SHARED** (`src/media/rtsp/CMakeLists.txt:33`) | Yes but singleton-bound — `RtspServer::getInstance/shutdown` (`RtspServer.h:25,43`) |
| SQLite DB file-info | `storage` | **STATIC** (`.a` artifact, `src/storage/CMakeLists.txt:1`, no SHARED keyword, no `BUILD_SHARED_LIBS`) | Cleanest — `DatabaseManager/MetadataDao/MediaScanner` |
| Upload transport | `network` | **SHARED** (`src/network/CMakeLists.txt:3`) | Clean — `MgmtServClient/StorageServClient` (`*.h`) |
| JSON manifest content | (none — app-inline) | — | **NO** — `generateDescInfo` lives in `main_app.cpp:268` |

So the **real gap is NOT "build an SDK"** — it is **app-inline orchestration fused into `main_app.cpp`**. The two genuinely missing pieces:

1. **No JSON-manifest capability lib.** `generateDescInfo` (`main_app.cpp:268-360+`) and `createDescInfoFile` (`:453`) are `static` functions inside `main_app.cpp`, fused to three singletons (`Settings`, `MCU`, `DeviceConfig`) and to `Disk`/`CRC`/`Timezone`. This is the single biggest extraction target.
2. **No mode-orchestration seam.** The per-mode command-bitmask dispatch (`main_app.cpp:1158-1202`) and its sequential execution (`:1310-1838`) is one 500-line block in `main()`. `app_workmode` lib already exists (`src/app/workmode/CMakeLists.txt:16`, SHARED) but holds **only the enum + `getWorkingMode()` reader** (`WorkMode.h:5-12`, `WorkMode.cpp`) — it does **not** execute the mode. That lib is the natural home for the new orchestration seam.

`htc_wifi_app` (`src/app/wifi_app.cpp` + `wifi_app_logic.cpp`, 430 lines total) is the proven thin-app precedent: a `main()` that calls `Misc::connectWifi/startDHCP` and exits. The workmode app should follow the same shape but with richer orchestration.

**Conclusion for the implementer:** Phase B is "move app-inline orchestration into SDK APIs" (chiefly `generateDescInfo` → a new manifest lib, plus an executor in `app_workmode`), and Phase C is "new `htc_workmode_app` + repoint the spawn at `media_app.cpp:257`".

---

## 2. `-wm` sub-mode inventory

Dispatch lives at `main_app.cpp:1157-1202` inside the `is_work_mode_cmd` branch. Format is `htc_main_app -wm <mode> -rtc <0|1>` (parsed at `:1158-1161`; the `-rtc` arg sets `is_rtc_work_well`). Each mode maps to a `command` bitmask; bits execute sequentially at `:1310-1838`. Startup (always, mode-independent): Settings load `:1217`, DeviceConfig/program-type `:1220`, SD mount `:1227/1239`, factory-config import `:1279`, DB init `:1004`, MediaScanner `:1019-1035`, EasyLogger `:1038-1060`, DayNight `:1061`.

| `workingMode` | Value | `command` bitmask | Ordered execution steps (file:line) |
|---|---|---|---|
| `WORKING_MODE_SNAP_ONLY` | 0 | `CMD_SNAP` | Settings/devconf/mount/startup (`:1217-1061`) → **snap** branch `CMD_SNAP && is_rtc_work_well` at `:1337` → `processCmdSnap` (`:555-624`): move quick-snap files, `createDescInfoFile` (`:608`) → shutdown `:1840` |
| `WORKING_MODE_SNAP_UPLOAD` | 1 | `CMD_SNAP \| CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` | startup → snap (`:1337-1358`) → `CMD_CONN_NET` `Misc::connectWifi` (`:1375`) → `CMD_DHCP` `Misc::startDHCP` (`:1403`) → `CMD_NTP` `Misc::ntpSync` + 30s wait-loop (`:1409-1454`) → `CMD_AUTH/UPLOAD` MgmtServClient connect+auth (`:1690-1714`) → upload loop `:1716-1838` → shutdown |
| `WORKING_MODE_UPLOAD_ONLY` | 2 | `CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` (+ led blink 60) | same as SNAP_UPLOAD minus the snap branch; RGB led `asyncBlink(60)` at `:1178` |
| `WORKING_MODE_TEST_ONLY` | 3 | `CMD_MOBILE` (+ led blink 30) | **OVERLAP** → identical path to `-m`/`--mobile`: `CMD_MOBILE` block `:1546-1666`: DayNight init, `connectWifi`/`startDHCP` (`:1574/1578`), mDNS `MdnsService::start` (`:1601`), HTTP server (`:1607-1623`), TcpEventService (`:1625`), RTSP singleton (`:1631-1645`), run-loop `:1649`, teardown `:1653-1665` |
| `WORKING_MODE_UVC` | 4 | `CMD_CONN_NET \| CMD_DHCP \| CMD_RTSP_SERVER` | **OVERLAP** → RTSP portion identical to `-rs`/`--rtsp-server`: `CMD_RTSP_SERVER` block `:1668-1688`: `RtspServer::getInstance()->start` (`:1679`), run-loop `:1683`, `stop()` `:1687`. (CONN_NET/DHCP bits execute at `:1367-1407` before this.) |

### Overlaps to flag explicitly (acceptance: these must be called out in the design doc)

- **`TEST_ONLY(3)` ≡ `-m`/`--mobile`.** Both set `command = CMD_MOBILE` (`:1186` vs `:1141`). Same execution block, same `HTC_TEST_MODE=1` env, same mDNS+HTTP+TCP+RTSP stack. Extracting TEST_ONLY means extracting the entire mobile stack — they are one capability-cluster, not two.
- **`UVC(4)` RTSP portion ≡ `-rs`/`--rtsp-server`.** Both reach `RtspServer::getInstance()->start()` (`:1679` vs the `-rs` dispatch at `:1150-1156`). UVC additionally does CONN_NET+DHCP first (which `-rs` alone does not). The RTSP-server capability is shared between UVC and the standalone `-rs` user-mode command.
- **`SNAP_ONLY(0)` share = `processCmdSnap`, used by `-s`/`--snap` too** — but note `-wm 0`/`-wm 1` reach the `is_rtc_work_well`-guarded snap branch (`:1337`), while `-s` reaches `CMD_SNAP && !is_rtc_work_well` (`:1456`). Two snap branches exist for the two RTC outcomes.

### Important nuance the inventory revealed (corrects a seed assumption)

`processCmdSnap` (`:555`) does **not** call `ImageSnap::snap` on the `-wm` path. It **moves pre-existing quick-snap files** produced by `htc_media_app -qs` (`QUICK_SNAP_INFO_FILE`, `:562-567`). The only `ImageSnap::snap` call in `processCmdSnap` is the `if (file_names.empty())` "only for test" block (`:611-621`). Likewise `processCmdVideoRecord` (`:626`) and `processCmdConcurrentSnapRecord` (`:725`) use `CameraRecorder`/`VideoRecorder` directly but do **not** wire DB rows or thumbnails in the `-wm` path — the manifest comes from `generateDescInfo`, not from `storage`. So capability 3 (photo+thumb) and 7 (DB) are **loosely coupled** to work mode today; capability 8 (JSON manifest) is the tight one.

---

## 3. Per-capability SDK inventory (the 8)

Format: impl → lib(target) → current public API → what the caller wires by hand → **gap** (what must leave `main_app.cpp` to make it a clean SDK API).

### (1) Network / WiFi / DHCP connect
- **Impl:** `src/common/misc/Misc.cpp` (connect+DHCP live here; driver-reuse logic `Misc::isWifiDriverLoaded/isWifiConnected`).
- **Lib:** `common_misc`, **SHARED** (`src/common/misc/CMakeLists.txt:20`). Already extracted to a standalone app: `htc_wifi_app`.
- **Public API:** `Misc.h:31-32` — `static bool connectWifi(const std::string &ssid, const std::string &password)`; `static bool startDHCP(const std::string &ifname="")`.
- **Caller wires by hand today:** SSID/pwd lookup from `DeviceConfig` (`INI_KEY_CSSID/CPWD`) at `:1370-1373` and again at `:1570-1573`; empty-check `:1568`; interface-name selection by `program_type` at `:1244-1253`; run-loop none (fire-and-forget).
- **Gap:** SSID/pwd/iface resolution + the `BUILD_FOR_SIMULATION` bypass should move into a small `WifiConnector` helper (or into `app_workmode`) so `-wm 1/2/3` and `-wm 4` share one connect step instead of two duplicated blocks (`:1367-1407` vs `:1568-1582`).

### (2) NTP sync
- **Impl:** `Misc::ntpSync` in `common_misc`.
- **Lib:** `common_misc` SHARED.
- **Public API:** `Misc.h:36` — `static bool ntpSync(const std::string& ntp_server)`.
- **Caller wires by hand today:** server ip:port assembly from `DeviceConfig` (`INI_KEY_NTP_IP/PORT`) at `:1410-1414`; **the 30s `tm_year > YEAR_MIN` wait-loop is entirely in `main_app.cpp:1419-1440`**; RTC writeback `if (is_rtc_work_well) RTC::setTime` at `:1447-1449`.
- **Gap:** the wait-until-synchronized loop + RTC writeback is app-inline and duplicated conceptually with the RTC flow. Move into `common_misc` (e.g. `Misc::ntpSyncAndWait(server, timeout, rtc=nullptr)`) so `-wm 1/2` and `-n`/`-qs` share it.

### (3) Photo capture + optional thumbnail
- **Impl:** `src/media/snap/ImageSnap.cpp`.
- **Lib:** `media_snap`, **SHARED** (`src/media/snap/CMakeLists.txt:21`); `PUBLIC hal_video ... storage common_utils_jpeg`.
- **Public API:** `ImageSnap.h:42-49` — `bool snap(const std::string&)` (+ vector/async overloads); thumbnail via `getThumbnailData()`/`hasThumbnail()`, `THUMB_STREAM_ID 2` (`:16`).
- **Caller wires by hand today:** on the `-wm` path the caller does **not** capture — it moves files (`processCmdSnap :555`). `ImageSnap` is only used in the test fallback (`:614-619`). The actual quick-snap capture happens in `htc_media_app` (`-qs`), not `main_app`.
- **Gap:** none blocking for `-wm` extraction. For Phase B, the design doc should note that work mode does **not** own photo capture today (it's in `media_app`); moving capture into a workmode app would change the existing `media_app → main_app` split. Recommend **leaving capture in `media_app`** in Phase C and having the workmode app only orchestrate post-capture (move + manifest + upload).

### (4) Video record + optional thumbnail
- **Impl:** `src/media/video/VideoRecorder.cpp` + `src/service/camera/CameraRecorder.cpp`.
- **Lib:** `media_recorder` **SHARED** (`src/media/video/CMakeLists.txt:18`) + `camera_service` STATIC (`src/service/camera/CMakeLists.txt:6`).
- **Public API:** `CameraRecorder` (`service/camera/CameraRecorder.h`) — async `record()` with `RecordOptions{audio,autoCover}`; `processCmdVideoRecord :626-723` builds `RecordOptions` inline.
- **Caller wires by hand today:** `processCmdVideoRecord` (`:626`) constructs `CameraRecorder`, sets `opts.audio/autoCover`, env-driven `HTC_RECORD_TMPFS` path (`:640-652`) and `HTC_RECORD_BITRATE_KBPS` (`:670`); `processCmdConcurrentSnapRecord` (`:725`) builds full `VideoParams/AudioParams` inline (`:735-756`). `generateDescInfo` is called after (`:716`).
- **Gap:** the record-options assembly + diagnostic env hooks are app-inline and duplicate logic with `media_app`'s recorder usage. Move a `WorkModeRecorder` config builder into `app_workmode` (not into the capability lib — the env diagnostics are policy).

### (5) mDNS discovery
- **Impl:** `src/service/discovery/MdnsService.cpp`.
- **Lib:** `discovery_service`, **STATIC** (`src/service/discovery/CMakeLists.txt:6`).
- **Public API:** `MdnsService.h:25-32` — singleton `getInstance()`, `bool start(const MdnsServiceParams&)`, `void stop()`. **Lowest coupling** of the eight.
- **Caller wires by hand today:** `buildMdnsParams(config, iface, ip, http_port, rtsp_port)` at `:239-266`; enable-check `isMdnsEnabled(config)` at `:1599`; port lookup `getConfiguredPort(...)` for `INI_KEY_MDNS_CTRL_PORT/MDNS_RTSP_PORT`.
- **Gap:** `buildMdnsParams` + port resolution is app-inline. Move into `discovery_service` (or `app_workmode`) so `CMD_MOBILE` (`:1601`) and any future mobile-like mode share it. Small gap — this capability is already clean.

### (6) RTSP server
- **Impl:** `src/media/rtsp/RtspServer.cpp` (+ `MediaSession`).
- **Lib:** `media_rtsp`, **SHARED** (`src/media/rtsp/CMakeLists.txt:33`).
- **Public API:** `RtspServer.h:23-43` — singleton `getInstance()`, `static registerOnsessionClosedCallback`, `bool start()/stop()`, and **process-lifetime `void shutdown()`** (`:43`) which releases HAL.
- **Caller wires by hand today:** port from `DeviceConfig` (`:1629/1674`); `rtsp_singleton_used = true` guard (`:1630/1670`) so the shutdown path at `:1854-1859` only touches the singleton if it was actually used; the `registerOnsessionClosedCallback` lambda is duplicated at `:1631` and `:1675`; run-loop `while(!already_in_exit_flow)` at `:1649` and `:1683`; teardown ordering in `main_exit` at `:1848-1859`.
- **Gap:** the `rtsp_singleton_used` guard + the duplicate start/run/stop sequence (mobile `:1631-1657` vs rtsp-only `:1675-1687`) is the core duplication. Phase B should lift a `startRtspUntilSignal(port)` helper. **Constraint:** `shutdown()` is process-terminal (HAL teardown) — it must stay in the app's exit path, not move into a long-lived lib call. Caps 3/4/6 treat HAL as opaque (PIC-owned `src/hal/**`).

### (7) SQLite DB file-info
- **Impl:** `src/storage/{DatabaseManager,MetadataDao,MediaScanner}.cpp`.
- **Lib:** `storage`, **STATIC** (verified: `build/src/storage/libstorage.a`; `CMakeLists.txt:1` has no SHARED keyword and project sets no `BUILD_SHARED_LIBS`; `POSITION_INDEPENDENT_CODE ON` at `:8`).
- **Public API:** `DatabaseManager::getInstance().init(db_path)`; `MetadataDao`; `MediaScanner::getInstance().startScan(opts)` (`MediaScanner.h:31-32`).
- **Caller wires by hand today:** DB init at `:1004`; `MediaScannerOptions` build + `startScan` at `:1019-1035`; nothing in the `-wm` per-mode path writes DB rows (manifest is JSON, not DB).
- **Gap:** none for `-wm`. Cleanest capability. Phase B/C should keep DB owned by whichever app boots first (currently `main_app`); a future split may need both apps to share one DB file path.

### (8) JSON manifest generation + server upload
- **Impl (split):**
  - **Transport** (clean): `src/network/{MgmtServClient,StorageServClient,Client}.cpp`, lib `network` **SHARED** (`src/network/CMakeLists.txt:3`). API: `MgmtServClient::connect/authenticate/newStorageServClient`, `StorageServClient::uploadFile/bindUploadCallback/isUploadFinished` (`network/*.h`). `network` is the **dependency sink** — it links `setting env disk mcu common_time_rtc common_time_timezone power jsoncpp md5 ... common_misc common_utils_serial` (`network/CMakeLists.txt:37`).
  - **Content** (dirty): `generateDescInfo` `:268-360+` and `createDescInfoFile :453` are `static` in `main_app.cpp`, fused to `Settings::getInstance()`, `MCU::getInstance()`, `DeviceConfig::getInstance()`, `Disk`, `CRC`, `Timezone`, `Misc::getIPAddress/getFilepath/getFilename`.
- **Caller wires by hand today:** the entire upload orchestration — `MgmtServClient` connect+auth (`:1690-1714`), 8s desc-file wait-loop (`:1751-1768`), per-file upload loop + `F_UploadedTag` rewrite (`:1769-1833`), `FILE_MANAGE_DELETE` policy (`:1817`) — is one ~150-line block app-inline.
- **Gap (the biggest one):** `generateDescInfo`/`createDescInfoFile` must become a **new capability lib** (provisional name `manifest` or fold into `storage`/`network`). Until they move, any workmode app must still drag in the three singletons + `Disk`/`CRC`. This is the single highest-value Phase B extraction. The upload-loop orchestration (auth → desc-file → per-file → tag rewrite) should become an `UploadSession` in `network` or `app_workmode`.

---

## 4. Proposed layering

```
thin apps (htc_main_app [legacy/shrinking], htc_media_app, htc_workmode_app [new], htc_wifi_app, htc_daemon_app)
        │  only: arg parse + mode→command map + run-loop + shutdown ordering
        ▼
mode-orchestration libs  ← THE NEW SEAM
  - app_workmode (exists, enum-only today) → grows: command-bitmap executor,
    WifiConnector helper, startRtspUntilSignal, UploadSession driver,
    WorkModeRecorder config builder
        │  pure orchestration; no HAL, no direct singleton reads beyond config
        ▼
capability libs (the "SDK layer" — already exists)
  common_misc(SHR) media_snap(SHR) media_recorder(SHR)+camera_service(STATIC)
  discovery_service(STATIC) media_rtsp(SHR) storage(STATIC) network(SHR)
  [NEW] manifest lib — extracted from generateDescInfo
        │  clean public APIs; HAL treated as opaque
        ▼
HAL  src/hal/**  PIC-OWNED (AGENTS.md:142-143) — caps 3/4/6 must stay opaque
```

Dependency direction: apps → orchestration → capabilities → HAL. `network` is the **dependency sink** (pulls in nearly every config/hardware helper); keep it at the capability tier, never let orchestration link hardware directly.

### Phased roadmap

- **Phase A (this task, T8):** two design docs only. No code.
- **Phase B:** move app-inline orchestration into SDK APIs, **without** changing any binary's behavior:
  - B1: extract `generateDescInfo`/`createDescInfoFile` → new `manifest` lib (or `storage` extension). `htc_main_app` links it; behavior identical.
  - B2: lift NTP wait-loop into `common_misc`; lift `buildMdnsParams` into `discovery_service`; lift `startRtspUntilSignal` + the mobile/rtsp duplication into `app_workmode`.
  - B3: lift the upload orchestration into an `UploadSession`.
  - B4: grow `app_workmode` from enum-only into the command-bitmap executor (the body of `main_app.cpp:1310-1838`), still called by `htc_main_app`.
  - Gate: each B-step keeps `htc_main_app -wm <n>` behavior bit-identical (golden: same log sequence, same files produced, same upload).
- **Phase C:** new `htc_workmode_app` thin binary that calls `app_workmode` executor; repoint the spawn at `src/app/media_app.cpp:257` (`"htc_main_app -wm ..."` → `"htc_workmode_app -wm ..."`). Keep `htc_main_app` for the non-`-wm` user-mode commands (`-s/-u/-m/-rs/...`) during transition; retire later.
- **Out of scope:** touching `src/hal/**`; changing `htc_media_app`'s capture responsibility; splitting the single shared encoder pipeline across processes.

---

## 5. Constraints (must appear in the design doc)

1. **`src/hal/**` is PIC-owned** (`AGENTS.md:142-143`). Caps 3 (photo), 4 (video), 6 (RTSP) treat HAL as opaque; the orchestrator must never call HAL directly.
2. **Singleton coupling:** `Settings::getInstance()`, `MCU::getInstance()`, `DeviceConfig::getInstance()` are process-global. The manifest lib extraction (B1) must receive these via parameters, not call them, or the lib inherits the same fusion. `DayNightSwitch` singleton (`:1061`) likewise.
3. **Single shared encoder pipeline:** IMP encoder channel/group is process-scoped; the `RtspServer::shutdown()` teardown at `:1854-1859` exists precisely because the next boot would hang on stale IMP state. A workmode app that spans RTSP + record must own the same teardown ordering. Do **not** assume two processes can share the encoder.
4. **`network` is the dependency sink** (`network/CMakeLists.txt:37`). Any new orchestration lib that needs upload links `network` and thereby transitively most of the tree — keep orchestration thin.
5. **`htc_main_app` is NOT self-contained** — it dynamically links all `.so` in `build/lib/` (project CLAUDE.md). A new `htc_workmode_app` inherits the same `.so` runtime requirement; NFS deploy + `LD_LIBRARY_PATH` rules apply unchanged.
6. **Naming:** `-wm`/`--work-mode` = the 5 hardware sub-modes (`WorkMode.h:5-12`). `-w`/`--wifi` (`main_app.cpp:1111`) is a one-shot WiFi command, NOT a mode. Do not conflate.
7. **Dual-platform:** every extraction must compile under both `BUILD_FOR_SIMULATION=ON` and T32 (uClibc — no `std::to_string/stoi`, use `snprintf`/`strtol`). The dispatch already uses `stoi_custom`/`to_string_custom` (`:1160, 257`).

---

## 6. Proposed outline for the two design docs (implementer writes these in Phase A)

### `doc/design/workmode-sdk-architecture.md` (vision / roadmap / gap)
1. Problem statement: `main_app.cpp` 1894-line god-binary; `-wm` is one branch.
2. Thesis validation (section 1 above): SDK layer largely exists; gap is app-inline orchestration.
3. Layering diagram (section 4) + dependency rules.
4. Phased roadmap (A/B/C) with per-step behavior-preserving gates.
5. Constraints (section 5).
6. Open questions: should capture (cap 3) move from `media_app` to `workmode_app`? (Recommendation: no, keep the split.) Where does the manifest lib live — standalone vs `storage`?

### `doc/design/workmode-capability-inventory.md` (the two tables)
1. `-wm` sub-mode inventory table (section 2) incl. the two overlaps (TEST_ONLY≡`-m`, UVC-RTSP≡`-rs`).
2. Per-capability SDK inventory (section 3) — 8 rows, each with impl/lib/API/hand-wired/gap.
3. The "biggest gap" callout: `generateDescInfo` extraction.
4. Dependency-sink note on `network`.

---

## Evidence (commands that prove the file:line claims)

- `grep -n "WORKING_MODE_SNAP_ONLY\|WORKING_MODE_UVC\|enum workingMode" src/app/workmode/WorkMode.h` → enum at `:5-12`.
- `sed -n '1109,1202p' src/app/main_app.cpp` → dispatch + sub-mode→command map (`:1158` SNAP_ONLY, `:1175` UPLOAD_ONLY, `:1184` TEST_ONLY, `:1191` SNAP_UPLOAD, `:1194` UVC).
- `grep -n 'command & CMD_\|processCmd\|generateDescInfo\|createDescInfoFile\|MdnsService\|RtspServer\|connectWifi\|startDHCP\|ntpSync' src/app/main_app.cpp` → all execution sites.
- `sed -n '20p;3p;21p;18p;33p;6p;1p;16p' <each CMakeLists>` → lib types.
- `find /home/zengping/project/huntcam/code/t32_cam/build -name 'libstorage*'` → `libstorage.a` (STATIC proof).
- `sed -n '257p' src/app/media_app.cpp` → spawn point `htc_main_app -wm ...`.
