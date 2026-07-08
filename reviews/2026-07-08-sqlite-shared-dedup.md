# 2026-07-08 — SQLite / EasyLogger STATIC→SHARED 去重

## 背景
`libmedia_recorder.so` / `libmedia_snap.so` 各 ~1.14MB。反汇编归因发现 `.text` 的 **83% (~843KB) 是 SQLite amalgamation**：因 `storage`(STATIC) `PUBLIC`-link 了 `sqlite3`(STATIC)，被每个 link `storage` 的 SHARED 库整份静态吸收。mp4 mux (`minimp4`) 只占 <0.1% —— 大 size 与 mp4 无关。EasyLogger 同理 (9KB×N)。详见 `media_recorder.so` 的 `objdump` 前缀分类。

## 改动
- `third_party/sqlite/CMakeLists.txt`:
  - `STATIC` → `SHARED`，设 `LIBRARY_OUTPUT_DIRECTORY=${CMAKE_BINARY_DIR}/lib`
  - 删 `SQLITE_ENABLE_RTREE`（grep 确认项目无空间索引用法）
  - 删 `SQLITE_ENABLE_JSON1`（SQLite 3.46 已默认内置 JSON，define 无效）
  - 加 `SQLITE_OMIT_DEPRECATED`
  - compile_definitions 保持 `PUBLIC`（OMIT/ENABLE 宏影响 `sqlite3.h` 条件声明，消费者须一致）
- `third_party/easylogger/CMakeLists.txt`: `STATIC` → `SHARED`，加 `LIBRARY_OUTPUT_DIRECTORY`

## 体积 (T32 `build/`, strip 后 — 真实 flash 占用)
| 库 | before | after | delta |
|----|--------|-------|-------|
| libmedia_recorder.so | 1,174,076 | 102,748 | **-91%** |
| libmedia_snap.so     | 1,175,416 | 106,012 | **-91%** |
| libsqlite3.so (新增) | — | 1,028,168 | 全局唯一一份 |
| libeasylogger.so (新增) | — | 18,184 | 全局唯一一份 |

**净省 ~1.04 MB**（2 份 SQLite → 1 份）。未来任何 link `storage` 的 SHARED 库不再重复背 SQLite。

## 验证
- 双平台 (`build_sim` + T32 `build`) 编译/链接通过，exit 0。
- `media_recorder` / `media_snap` / `app_workmode` / `app_lifecycle` 的 `NEEDED` 正确含 `libsqlite3.so` + `libeasylogger.so`，`ldd` 解析正常。
- `app_workmode/lifecycle` 体积几乎不变 → 证实它们本就没静态吸收 SQLite（仅传递性 NEEDED）。

## 部署注意
新 `.so` 自动产出到 `build/lib/`，随 NFS 挂载部署，设备现有 `LD_LIBRARY_PATH=./lib` 启动方式天然满足，无需改启动脚本。

## 待设备回归
录影（mp4 mux + DB metadata 写入）、抓拍（含 `LargeImageSnap` 大图分条带路径）、DB WAL 读写、elog 异步输出。

## 风险评估
- `SQLITE_OMIT_DEPRECATED`：编译通过 = 未调用废弃 API，编译期已验证安全。
- `RTREE` 移除：grep 确认无 RTree / 空间索引 SQL。
- SQLite 跨 .so C ABI 传递 `sqlite3*` 句柄无问题。

---

## 第二轮：civetweb + service 链 STATIC→SHARED（同日）

### 背景
`libapp_workmode.so` 仍有 605KB。反汇编归因：吸收的 STATIC 库 **civetweb(103KB, HTTP 栈) + camera_service(246KB, 业务) + http_server(131KB) + mcu/event/discovery_service** 被 `app_workmode` + `app_lifecycle` + 各可执行(`main_app`/`media_app`/...) **重复静态吸收**。

### 改动（6 库 SHARED + 1 bug 修复）
- `third_party/civetweb/CMakeLists.txt`: STATIC→SHARED + `LIBRARY_OUTPUT_DIRECTORY`
- `src/service/{http_server,event,discovery,camera,mcu}/CMakeLists.txt`: STATIC→SHARED + `LIBRARY_OUTPUT_DIRECTORY`
- `device/sensor/storage/system_service` 保持 STATIC（仅 http_server 用 → 吸收进 `libhttp_server.so`，合理集中）
- `src/common/misc/CMakeLists.txt`: 补 `logger` link —— 修 underlinking（common_misc 用 `Logger::log` 却没 link logger，discovery 改 SHARED 后 test 链接暴露此 bug）

### 连锁核查（决策依据）
`http_server` PUBLIC-link `camera_service`(3 处用) → `camera` PUBLIC-link `mcu_service`(2 处用) → `mcu` link `mcu`/`logger`(SHARED, 收口)。`camera_service`/`mcu_service` 多处 link，是 http_server SHARED 化的必要配套（否则重复加剧）。

### 体积（build_sim NET = **-5382 KB**）
| | before | after | delta |
|--|--------|-------|-------|
| .so 总 | 33629 KB | 35971 KB | +2342 |
| bin 总 | 33269 KB | 25544 KB | **-7727** |
| **合计** | 66899 KB | 61516 KB | **-5382** |

关键：**可执行**（main_app/media_app/daemon_app/workmode_app/wm/um）释放了重复吸收的 camera/civetweb/mcu/service —— 这是 -7.7MB 的主体。表面 `.so` 增掩盖了真相，必须算 bin。

T32（strip）：`app_workmode` 605→274KB(-55%)，`app_lifecycle` 392→69KB(-82%)；各可执行仅 30–37KB（全动态依赖，不再含 ~400KB 静态吸收）。

### 待设备回归
HTTP API（civetweb/camera/http_server 动态）、mDNS（discovery）、TCP 事件、MCU、录影/抓拍（camera_service）、common_misc 日志路径。

### 教训
SHARED 化收益必须算**全部产物**（.so + 可执行），只看单个 .so 会误判（.so 增、可执行大降）。--gc-sections 下 STATIC 各消费者只吸收引用子集，但多消费者 × 子集之和常 > 完整库 ×1，故 SHARED 化被多处 link 的大库净收益显著。
