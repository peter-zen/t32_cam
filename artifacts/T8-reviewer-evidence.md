# T8 Reviewer Audit Evidence

> Independent semantic/factual audit of the two Phase A design docs:
> - `doc/design/workmode-sdk-architecture.md`
> - `doc/design/workmode-capability-inventory.md`
>
> Method: every claim re-verified against the actual source in worktree `feature/new-workmode`
> via Read/Grep. `src/app/main_app.cpp` is confirmed 1894 lines.

## Scope gate (Phase A = docs only, no src/)

| Check | Result |
|---|---|
| `git status --short` shows NO `src/` modification | PASS — only `doc/`, `artifacts/`, `doc/knowledge/working-set.md`, `orchestration-state.yaml` |
| Both design docs are new (untracked) | PASS |
| HAL PIC-owned untouched | PASS — no `src/hal/**` change |

## 1. `-wm` sub-mode → command-bitmask map (main_app.cpp switch, :1166-1195)

| workingMode | enum value | doc claim | actual source | Verified |
|---|---|---|---|---|
| `SNAP_ONLY` | 0 | `CMD_SNAP` | `command = CMD_SNAP` @ :1168 | PASS |
| `SNAP_UPLOAD` | 1 | `CMD_SNAP \| CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` | @ :1185 | PASS |
| `UPLOAD_ONLY` | 2 | `CMD_CONN_NET \| CMD_DHCP \| CMD_NTP \| CMD_UPLOAD` (+ blink 60) | @ :1171; blink(60) @ :1173 | PASS |
| `TEST_ONLY` | 3 | `CMD_MOBILE` (+ blink 30) | @ :1181; blink(30) @ :1179 | PASS |
| `UVC` | 4 | `CMD_CONN_NET \| CMD_DHCP \| CMD_RTSP_SERVER` | @ :1188 | PASS |

Enum source (`src/app/workmode/WorkMode.h:5-12`): SNAP_ONLY=0, SNAP_UPLOAD=1, UPLOAD_ONLY=2, TEST_ONLY=3, UVC=4 — matches doc table values exactly. PASS.

Parsing `working_mode`/`is_rtc_work_well` via `stoi_custom` at :1163-1164 — matches doc. PASS.

## 2. The two overlaps

- **TEST_ONLY(3) ≡ `-m`:** `-m` sets `command = CMD_MOBILE` @ :1130 (dispatch arm `:1129`); TEST_ONLY sets `command = CMD_MOBILE` @ :1181. Both reach the `if (command & CMD_MOBILE)` block @ :1546, same `HTC_TEST_MODE=1` env (@ :1547), same mDNS+HTTP+TCP+RTSP stack. **PASS.**
- **UVC(4) RTSP ≡ `-rs`:** `-rs` dispatch @ :1145 → `command = CMD_RTSP_SERVER`; UVC sets `CMD_RTSP_SERVER` @ :1188. Both reach `if (command & CMD_RTSP_SERVER)` @ :1668 and call `RtspServer::getInstance()->start()` @ :1679. **PASS.**
- **SNAP_ONLY(0) vs `-s` snap branches:** `CMD_SNAP && is_rtc_work_well` @ :1337; `CMD_SNAP && !is_rtc_work_well` @ :1456. Both confirmed. PASS.

## 3. Per-capability lib types (8 capabilities)

| Capability | Doc claim | Actual `add_library` | Verified |
|---|---|---|---|
| Network/WiFi/DHCP/NTP | `common_misc` SHARED | `add_library(common_misc SHARED ...)` @ `src/common/misc/CMakeLists.txt:20` | PASS |
| Photo + thumb | `media_snap` SHARED | `add_library(media_snap SHARED ...)` @ `src/media/snap/CMakeLists.txt:21` | PASS |
| Video record | `media_recorder` SHARED + `camera_service` STATIC | `media_recorder SHARED` @ `src/media/video/CMakeLists.txt:18`; `camera_service STATIC` @ `src/service/camera/CMakeLists.txt:6` | PASS |
| mDNS | `discovery_service` STATIC | `add_library(discovery_service STATIC ...)` @ `src/service/discovery/CMakeLists.txt:6` | PASS |
| RTSP server | `media_rtsp` SHARED | `add_library(media_rtsp SHARED ...)` @ `src/media/rtsp/CMakeLists.txt:33` | PASS |
| SQLite DB | `storage` STATIC | `add_library(storage ...)` @ `src/storage/CMakeLists.txt:1` — no SHARED keyword, no `BUILD_SHARED_LIBS`, PIC ON @ :8. STATIC confirmed. | PASS |
| Upload transport | `network` SHARED | `add_library(network SHARED ...)` @ `src/network/CMakeLists.txt:3` | PASS |
| JSON manifest | (none — app-inline) | no lib; `generateDescInfo`/`createDescInfoFile` are `static` in main_app.cpp | PASS |

### `storage` vs `storage_service` conflation check (tester-flagged)

- DB capability (DatabaseManager/MetadataDao/MediaScanner) is attributed to the **`storage`** target in both docs.
- `src/storage/CMakeLists.txt:1-5` `add_library(storage DatabaseManager.cpp MetadataDao.cpp MediaScanner.cpp)` — these three files ARE in `storage`. **PASS — doc attribution correct.**
- A separate `storage_service` STATIC exists at `src/service/storage/CMakeLists.txt:6` but does NOT contain DatabaseManager/MetadataDao/MediaScanner. The doc correctly distinguishes them; no conflation present.

### `network` dependency-sink claim

- `target_link_libraries(network PRIVATE setting env disk mcu common_time_rtc common_time_timezone power jsoncpp md5 pthread common_utils_base64 common_misc common_utils_serial)` @ `src/network/CMakeLists.txt:37` — matches doc verbatim. PASS.

## 4. Biggest-gap claim (the highest-value extraction target)

- `generateDescInfo` is `static int generateDescInfo(...)` @ **main_app.cpp:268** — confirmed app-inline static, NOT in any lib. PASS.
- `createDescInfoFile` is `static int createDescInfoFile(...)` @ **main_app.cpp:453** — confirmed app-inline static. PASS.
- Singleton fusion: `generateDescInfo` reads `Settings::getInstance()` @ :270, `MCU::getInstance()` @ :272, `DeviceConfig::getInstance()` @ :316, plus `Disk` @ :337, `CRC` @ :305, `Timezone` @ :277, `Misc` @ :299-318. **"fused to three singletons + Disk/CRC/Timezone" claim fully substantiated.** PASS.

## 5. `app_workmode` is enum-only; `htc_wifi_app` precedent

- `src/app/workmode/` contains only `WorkMode.h`, `WorkMode.cpp`, `CMakeLists.txt`. `WorkMode.cpp` is enum + `getWorkingMode()` reader (GPIO/MCU pin read) — no mode execution. SHARED @ `CMakeLists.txt:16`. "enum-only, does not execute the mode" — PASS.
- `htc_wifi_app` precedent: `src/app/wifi_app.cpp` + `wifi_app_logic.cpp` + `.h` exist — the thin-app precedent claim is grounded. PASS.

## 6. Phase-A scope / HAL / spawn / misc

| Claim | Source | Verified |
|---|---|---|
| `main_app.cpp` = 1894 lines | `wc -l` = 1894 | PASS |
| spawn point `media_app.cpp:257` (`"htc_main_app -wm ..."`) | exact match @ :257 | PASS |
| `RtspServer::shutdown()` HAL release @ main_exit :1857 | `RtspServer::getInstance()->shutdown()` @ :1857, guarded by `rtsp_singleton_used` @ :1856 | PASS |
| `main_exit:` label @ :1840 | confirmed @ :1840 | PASS |
| `buildMdnsParams` @ :239 | confirmed @ :239 | PASS |
| NTP `Misc::ntpSync` + 30s wait @ :1418-1450 | ntpSync @ :1418; `tm_year > YEAR_MIN` wait @ :1423-1441 | PASS |
| `processCmdSnap` @ :555 moves quick-snap files (not `ImageSnap::snap`) on `-wm` path | `processCmdSnap` @ :555; reads `QUICK_SNAP_INFO_FILE` @ :559 | PASS |
| `-w`/`--wifi` is one-shot CMD_CONN_NET, NOT a mode | @ :1111-1112 | PASS |
| `src/hal/**` PIC-owned per `AGENTS.md` | `AGENTS.md:142-143` confirms | PASS |
| Dual-platform / uClibc no `std::to_string/stoi` — uses `stoi_custom`/`to_string_custom` | `stoi_custom` @ :1163; `to_string_custom` @ media_app.cpp:257 | PASS |

## 7. Trivial line-number drift (not fixed — within tolerance)

A handful of off-by-N line refs exist (doc → actual):
- `-m` dispatch `:1130` → arm @ :1129, `command=CMD_MOBILE` @ :1130 (the doc cites the assignment line — acceptable).
- UPLOAD_ONLY blink(60) `:1172` → actual :1173 (off-by-one).
- `CMD_MOBILE` block `:1546-1665` → actual closes @ :1666 (off-by-one).
- `CMD_RTSP_SERVER` block `:1668-1687` → actual closes @ :1688 (off-by-one).
- desc-file wait `:1751-1768` → actual 8s loop body @ :1768-1781 (approx).
- `FILE_MANAGE_DELETE` `:1817` → actual :1822 (off-by-5).

These drift by 1-5 lines and do NOT change the meaning of any claim. The docs explicitly state line numbers are approximate and note `main_app.cpp` is 1894 lines. No fix applied — below the "trivial nit worth editing" threshold and editing would risk introducing churn for zero semantic gain.

## Verdict

**APPROVE.** All material/factual claims verified against source. No substantive errors found. No capability mis-characterized. The thesis ("SDK layer already exists; real gap is app-inline orchestration in `main_app.cpp`, chiefly `generateDescInfo`/`createDescInfoFile`") is sound and code-grounded. The tester-flagged `storage` vs `storage_service` concern is a non-issue — the doc correctly attributes the DB capability to `storage`. Only off-by-N line drift exists, within tolerance, no fixes applied.
