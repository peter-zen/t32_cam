# T15 (Phase C-2) — Planner Full Design

Extract the command-execution cascade out of `src/app/main_app.cpp::main()` into
`app_workmode`, behavior-preserving, so `htc_main_app -wm` (and the future
`htc_workmode_app`) calls it. C3 (new app) and C4 (repoint spawn) are out of scope.

All line numbers below are against the **current** tree
(`git show HEAD:src/app/main_app.cpp` = 1200 lines, post-T14), NOT the Phase C
plan's pre-T14 numbers (`:1003-1492` etc.). T14 already moved startup/signal/
shutdown into `app_lifecycle::ProcessLifecycle`; the cascade + dispatch stayed
in `main_app.cpp` with refs rewritten through `lc.`.

---

## 0. Ground truth (confirmed by reading the current tree)

| Region | Current lines | What |
|---|---|---|
| CMD_* bitmask `#define`s | `:450-462` | CMD_CONN_NET…CMD_SET_RTC |
| TU-local mode state | `:464-467` | `rtsp_audio_enabled`, `mobile_rtsp_enabled`, `mgmtServClient`, `storageServClient` |
| `main()` dispatch | `:527-620` | single-shot flag parse (`:528-578`) + `-wm` switch→bitmap (`:579-619`) |
| `lc.commonStartupPostDispatch` | `:624` | S9-S13 |
| `lc.setCleanupHook` | `:634-672` | mode-local resets (incl. `mgmtServClient=nullptr`/`storageServClient=nullptr` at `:656-657`) |
| **The cascade** | **`:681-1170`** | the `if(command & CMD_*)` cascade |
| `main_exit:` label + tail | `:1172-1199` | `lc.shutdown(sctx)` + sim `_exit`/HW `syncWithMCU`+`poweroff` |
| Static helpers the cascade calls | `:74` `getCurrentTimeFormatted`, `:96` `getConfiguredPort`, `:190` `processCmdSnap`, `:261` `processCmdVideoRecord`, `:360` `processCmdConcurrentSnapRecord` | **these are `-wm`-reachable** (CMD_SNAP calls `processCmdSnap*`; CMD_MOBILE/CMD_RTSP call `getConfiguredPort`) |

**Key consequence:** the cascade is NOT a closed body of pure command-gated
logic. It calls 5 `static` helpers (`getCurrentTimeFormatted`,
`getConfiguredPort`, `processCmdSnap`, `processCmdVideoRecord`,
`processCmdConcurrentSnapRecord`) that live in `main_app.cpp`. Four of the five
are reachable from `-wm` (SNAP_ONLY/SNAP_UPLOAD → CMD_SNAP → `processCmdSnap*`;
TEST_ONLY → CMD_MOBILE, UVC → CMD_RTSP_SERVER → `getConfiguredPort`).
`getCurrentTimeFormatted` is transitive (called by the `processCmd*` helpers and
by CMD_VIDEO_RECORD/AUDIO_RECORD). Therefore `runCommands` CANNOT be a
standalone TU that calls those `static` helpers — they must move with the
cascade (see §3). This is the single most important structural fact for C2.

**The cascade reads `argv` exactly once**, at `:693` inside `CMD_SET_RTC`
(`std::string rtc_time = argv[2];`). `-wm` never sets `CMD_SET_RTC` (only `-srtc`
does). So `runCommands` does not need `argc/argv` for `-wm` correctness, but the
single-shot `-srtc` path (which also calls `runCommands`) does. Decision in §3.

**`goto main_exit` sites inside the cascade** (the reason a `goto`-free
extraction is needed): every `goto main_exit` at `:712,716,719,723,730,744,748,
754,759,764,768,776,787,790,804,808,811,815,822,916,920,924,940,947,958,987,
1018,1031,1038` is a "abort the cascade, run the shutdown tail" jump. `goto`
cannot cross a function boundary, so `runCommands` must return a status and the
caller (`main`) must run the shutdown tail. Decision in §6.

---

## 1. Goals / Non-goals

### Goals
1. The whole `if(command & CMD_*)` cascade (`main_app.cpp:681-1170`) executes
   from `app_workmode` instead of `main_app.cpp::main()`, with byte-identical
   behavior, reachable by BOTH `htc_main_app -wm <mode> -rtc <n>` AND the
   existing single-shot flags (`-s/-qs/-u/-m/-rs/-ar/-vr/-grtc/-srtc/-a/-hb/-w/-
   d/-n`).
2. The cascade exists in exactly ONE place (no duplication between the `-wm`
   path and the single-shot path) — this is the headline design requirement.
3. The `switch(working_mode)→command-bitmap` map (`:584-613`, incl. the
   `lc.rgbLed()->asyncBlink` calls and the invalid-mode `Power::requestShutdown`
   + `sleep(10)` + `return -1`) moves to `app_workmode` as `runWorkMode`.
4. The 5 `-wm`-reachable static helpers move with the cascade (otherwise it
   won't compile).
5. `app_workmode` declares its real link deps PUBLIC (T10/T13 lesson) and links
   on T32 uClibc (the gate, R7).
6. Behavior byte-identical: moved cascade + switch byte-diff vs
   `git show HEAD:src/app/main_app.cpp` shows ONLY `static`/local→`ctx.`/
   `lc.` rewrites, no logic edits.

### Non-goals
- Do NOT create `htc_workmode_app` (C3/T16).
- Do NOT repoint `media_app.cpp:257` spawn (C4/T17).
- Do NOT touch `src/hal/**`.
- Do NOT retire `-wm`/`-m` from `htc_main_app` (that's C5/T18).
- Do NOT change `ProcessLifecycle`'s API (C1 is done; C2 consumes it read-only).
- Do NOT change the cascade's control flow or logic — pure relocation + ref
  rewrites + `goto main_exit`→`return` (§6).
- No `std::to_string`/`stoi` anywhere in new T32 code (use the existing
  `to_string_custom`/`stoi_custom`).

---

## 2. Impacted files

| File | Change | Notes |
|---|---|---|
| `src/app/workmode/WorkModeRunner.h` | **NEW** | `WorkModeContext`, `runCommands`, `runWorkMode` declarations; CMD_* `#define`s (or a shared header — see §4). |
| `src/app/workmode/WorkModeRunner.cpp` | **NEW** | The moved cascade + the moved `switch` + the 5 moved static helpers. The largest TU. |
| `src/app/workmode/WorkMode.h` / `.cpp` | unchanged | enum + `getWorkingMode` stay. |
| `src/app/workmode/RtspWorkMode.h` / `.cpp` | unchanged | T12 helper already called by the cascade (`:1015`); its DI lambdas now capture `ctx.lc` (was `lc`). |
| `src/app/workmode/CMakeLists.txt` | **EDIT** | add include dirs + PUBLIC link deps (§5). |
| `src/app/main_app.cpp` | **EDIT** | delete the cascade (`:681-1170`), the `-wm` switch (`:584-613`), the 5 static helpers (`:74,96,190,261,360`); rewiring (§4). |
| `src/app/CMakeLists.txt` | **EDIT (possibly none)** | `app_workmode` is already linked by `htc_main_app` in both sim (`:90`) and hw (`:244`-ish) blocks. Verify; no new link line expected unless dep propagation needs it. |

No `src/hal/**` change. No new app. No C3/C4.

---

## 3. Executor shape — RECOMMENDED: single `runCommands` + thin `runWorkMode`

### The decision (one shape, rejecting two alternatives)

**RECOMMENDED — shape A: one shared cascade, two thin entry points**

```
namespace app_workmode {

// Bundle of the cascade's TU-local dependencies. See §3.1.
struct WorkModeContext {
    app_lifecycle::ProcessLifecycle& lc;
    bool        isRtcWorkWell      = true;
    bool        mobileRtspEnabled  = true;   // -m --no-rtsp / default true
    bool        rtspAudioEnabled   = true;   // -m/-rs --no-audio / default true
    int         argc               = 0;      // only for CMD_SET_RTC's argv[2]
    char**      argv               = nullptr;
    std::shared_ptr<MgmtServClient>&   mgmtServClient;     // by REF (cleanupHook nulls it)
    std::shared_ptr<StorageServClient>& storageServClient; // by REF
};

// The WHOLE if(command & CMD_*) cascade, verbatim. Returns a status so the
// caller can run the shutdown tail where `goto main_exit` used to jump.
enum class CascadeResult { Completed, Aborted };   // Aborted == "was goto main_exit"
CascadeResult runCommands(int command, WorkModeContext& ctx);

// The switch(working_mode)→bitmap map (verbatim :584-613) + runCommands.
// Returns CascadeResult (Aborted covers BOTH the in-cascade goto AND the
// invalid-mode early return — see §6.1).
CascadeResult runWorkMode(enum workingMode mode, WorkModeContext& ctx);

}  // namespace app_workmode
```

- `runCommands(command, ctx)` = the WHOLE cascade (`:681-1170`) **including**
  the single-shot-only blocks (GET_RTC/SET_RTC/AUDIO_RECORD/VIDEO_RECORD/
  HEARTBEAT). Rationale: `-wm` never sets those bits, so they no-op for `-wm`
  for free; keeping them avoids splitting the cascade (which the task explicitly
  asks for). This is the whole point — the cascade is shared, not forked.
- `runWorkMode(mode, ctx)` = the `switch(working_mode)→command` map
  (`:584-613`, verbatim — incl. `lc.rgbLed()->asyncBlink(60/30)` and the
  `default:` invalid-mode `Power::getInstance()->requestShutdown()+sleep(10)`)
  then `return runCommands(command, ctx)`.
- `htc_main_app`:
  - `-wm` branch → `result = app_workmode::runWorkMode(mode, ctx);`
  - single-shot flags → keep their dispatch (`command` parsing + the `-m` env
    `setenv` of `HTC_NO_AUDIO`/`HTC_FORCE_RECORD_DAY_MODE`/`HTC_RECORD_STREAM_ID`
    at `:549-560`, and `-rs --no-audio` at `:564-570` — these STAY in dispatch,
    BEFORE `runCommands`, because the cascade only reads env) →
    `result = app_workmode::runCommands(command, ctx);`
  - both paths then: `goto main_exit` equivalent = `if (result == Aborted) {
    run shutdown tail } else { run shutdown tail }` — the tail runs
    unconditionally (see §6.2). There is no "completed vs aborted" branch in
    behavior; the distinction only encodes "did a `goto main_exit` fire". The
    tail is identical either way (the original `main_exit:` runs for both normal
    completion and every `goto main_exit`).

**REJECTED — shape B: move only the `-wm`-reachable blocks.** Drops
GET_RTC/SET_RTC/AUDIO_RECORD/VIDEO_RECORD/HEARTBEAT from `runCommands`. Problem:
(1) the single-shot flags (`-ar/-vr/-grtc/-srtc/-a/-hb`) then have NO cascade to
run — they'd need a second copy or stay in main_app, re-forking the cascade,
which is exactly what the task says to avoid; (2) it splits one cascade into two
bodies that will diverge. The plan's C2 row ("drop dead GET_RTC/SET_RTC/…") is
superseded by the task brief's explicit instruction to keep the whole cascade.
**Recommend keeping the whole cascade (shape A).** Flag the plan-vs-brief
divergence to the PM — the brief is the later, more specific instruction and
wins (per CLAUDE.md "authoritative sources").

**REJECTED — shape C: inject the cascade as ~15 callbacks.** Unusable (the plan
itself rejected this at the module level for `app_lifecycle`). `WorkModeContext`
already carries the small number of TU-local deps cleanly.

### 3.1 `WorkModeContext` — design rationale (the cascade's real TU-local deps)

Reading the cascade (`:681-1170`) line by line, the symbols it touches that are
NOT already reachable through `ctx.lc.*` or global singletons:

| Symbol | Used at | Becomes | Why |
|---|---|---|---|
| `is_rtc_work_well` | `:708,711,…,793,800,803,…` (CMD_SNAP both branches, CMD_NTP) | `ctx.isRtcWorkWell` | `-rtc` arg; not in lifecycle |
| `mobile_rtsp_enabled` | `:973,1000` (CMD_MOBILE rtsp gate) | `ctx.mobileRtspEnabled` | `-m --no-rtsp` tuning; default true |
| `mgmtServClient` | `:1028,1029,1036,1045,1050` (AUTH/HEARTBEAT/UPLOAD) + nulled in cleanupHook `:656` | `ctx.mgmtServClient` **by ref** | must be the SAME object the cleanupHook nulls — hence by `shared_ptr<>&`, not by value |
| `storageServClient` | `:1050,1093,…,1138,1143` (UPLOAD) + nulled in cleanupHook `:657` | `ctx.storageServClient` **by ref** | same |
| `argv[2]` | `:693` (CMD_SET_RTC only) | `ctx.argv` (only read when `command & CMD_SET_RTC`) | single-shot `-srtc` only; `-wm` never sets the bit |
| `config` / `program_type` | `:677-678` (assigned from `lc.config()`/`lc.programType()`) | `ctx.lc.config()` / `ctx.lc.programType()` directly | already exposed by ProcessLifecycle (C1) |
| `lc.daynight()` / `lc.rgbLed()` | `:890-907,643-645,590,…` | `ctx.lc.daynight()` / `ctx.lc.rgbLed()` | C1 accessors |
| `lc.keepRunning()` / `lc.waitForSignal()` / `lc.markRtspSingletonUsed()` | `:974,993,994,1014,1015-1017` | `ctx.lc.*` | C1 methods |

**`rtsp_audio_enabled` note:** the cascade does NOT read `rtsp_audio_enabled`
today (grep the `:681-1170` range: only `mobile_rtsp_enabled` is read at
`:973,1000`; `rtsp_audio_enabled` is set in dispatch `:553,568` but the cascade
never reads it — it's effectively dead-but-kept for intent, or read elsewhere).
It IS referenced in the cleanupHook-env story (`HTC_NO_AUDIO`). Decision: carry
`ctx.rtspAudioEnabled` for completeness/future, but it is **not load-bearing**
for C2 behavior. Keep it to avoid silently dropping a field; mark with a code
comment "set in dispatch, not currently read by the cascade".

**The `-m` env sets** (`HTC_FORCE_RECORD_DAY_MODE`/`HTC_NO_AUDIO`/
`HTC_RECORD_STREAM_ID`, dispatch `:554-558`) STAY in main_app's dispatch (set
before `runCommands`). The cascade only reads them via `std::getenv` (CMD_MOBILE
`:895`). So `WorkModeContext` does NOT carry them — env is the channel, exactly
as today. This matches the task brief.

**Why `mgmtServClient`/`storageServClient` are by-reference:** the cleanupHook
(closure `:634-672`, still in `main_app`) nulls them at `:656-657`. If the ctx
held them by value, nulling the copy would not null the cascade's instance. By
ref, the same `shared_ptr` is shared. So `main_app` owns the two `shared_ptr`
locals and passes them by ref into the ctx. Confirmed: today they are
file-static (`:466-467`); after C2 they become `main()`-locals passed by ref
(into both the ctx and the cleanupHook closure, which already captures by `[&]`).

### 3.2 The 5 static helpers MUST move with the cascade

`runCommands` is a different TU from `main_app.cpp`. It calls:
- `processCmdSnap` (`:711,715,726,803,807,818`) — `-wm`-reachable (CMD_SNAP)
- `processCmdVideoRecord` (`:718,722,810,814`) — `-wm`-reachable (CMD_SNAP camMode 1/2/3)
- `processCmdConcurrentSnapRecord` (`:729,821`) — `-wm`-reachable (CMD_SNAP camMode 3)
- `getConfiguredPort` (`:909,910,1013`) — `-wm`-reachable (CMD_MOBILE, CMD_RTSP_SERVER)
- `getCurrentTimeFormatted` (`:215,279,282,352,388,400,421,885`) — transitive, `-wm`-reachable via the `processCmd*` helpers and CMD_VIDEO_RECORD

All 5 are `static` in `main_app.cpp` → invisible to `WorkModeRunner.cpp`. They
MUST move. Move them as `static` (file-local) inside `WorkModeRunner.cpp` (they
are not part of the public API; only the cascade calls them). Body-verbatim
move; their own deps drive the CMake list (§5). `printUsage` (`:428`),
`normalizePath` (`:87`), `syncWithMCU` (`:108`), `parseIniFile` (`:143`) STAY in
`main_app.cpp` (not called by the cascade).

---

## 4. `main_app.cpp` rewiring (concrete)

### 4.1 CMD_* `#define`s (`:450-462`)

Move the 13 `#define CMD_*` into `WorkModeRunner.h` (or a tiny
`WorkModeCommands.h` included by both `WorkModeRunner.cpp` and `main_app.cpp`).
`main_app` still needs them: dispatch sets `command` (single-shot path) and
passes it to `runCommands`; `runWorkMode` builds the bitmap internally; the
`ShutdownContext.command` (`main_exit :1184`) needs the value too. Recommendation:
a shared `WorkModeCommands.h` with the 13 `#define`s, included by
`WorkModeRunner.h` and `main_app.cpp`. (Keeps the bitmask single-sourced; the
plan's concern about "single source of truth" for the command vocabulary is
honored.)

### 4.2 Dispatch (`:527-620`)

- Single-shot block (`:528-578`): **KEEP VERBATIM**, including the `-m` env
  `setenv`s (`:549-560`) and `-rs --no-audio` (`:564-570`). It sets `command`.
  The only change: instead of falling through to the cascade, the post-dispatch
  code calls `runCommands(command, ctx)`.
- `-wm` block (`:579-619`): REPLACE the body. New body parses
  `working_mode` + `is_rtc_work_well` (`:581-583`, KEEP — these are arg-parse,
  not command logic), then calls
  `result = app_workmode::runWorkMode(working_mode, ctx);` and jumps to the
  post-cascade tail. The `switch` (`:584-613`) and the `argc!=5` invalid-cmd
  branch (`:614-619`) MOVE INTO `runWorkMode` (the switch) / stay callable as
  the `default:`/invalid-mode handling (see §6.1 for the invalid-mode
  `Power::requestShutdown()+sleep(10)+return`).
  - WAIT — `runWorkMode` takes `mode`, so the `argc==5` check (`:580`) and the
    `argv[3]=="-rtc"` check stay in dispatch (they're argv-shape validation,
    not work-mode logic). `runWorkMode` assumes a valid `(mode, rtc)` pair.
    The `:614-619` "wrong argc" branch stays in dispatch (it's an argv error,
    calls `Power::requestShutdown()+sleep(10)+return -1` — process-terminal,
    not cascade).

### 4.3 The cascade (`:681-1170`)

DELETE from `main_app.cpp`. Body moves verbatim into `WorkModeRunner.cpp::runCommands`.
Refs rewritten per §3.1 table. The `goto main_exit` → `return CascadeResult::Aborted;`
(§6). Normal cascade completion → `return CascadeResult::Completed;`.

### 4.4 The post-cascade tail (was `main_exit:` `:1172-1199`)

STAYS in `main_app.cpp` (it is process-terminal: `lc.shutdown(sctx)` + sim
`_exit(0)` / HW `syncWithMCU()+config->flush()+Misc::poweroff()+while(1)`).
After both `runWorkMode` and `runCommands` return, `main()` runs the tail
unconditionally (see §6.2 for why there's no `Completed`/`Aborted` branch). The
`config` local (`:677 = lc.config()`) is still needed for `config->flush()`
(`:1193`) — keep the `config = lc.config();` alias line in main.

### 4.5 CleanupHook (`:634-672`)

STAYS in `main_app.cpp`. It captures `[&lc]` and reads `mgmtServClient`/
`storageServClient` (`:656-657`). After C2 those two become `main()`-locals
passed by ref into `WorkModeContext` — the closure must capture them too. Change
the capture to `[&lc, &mgmtServClient, &storageServClient]`. Body otherwise
verbatim. (This is the R3 risk surface — see §7.)

---

## 5. CMake — `src/app/workmode/CMakeLists.txt`

### 5.1 Why this is a real change

Today `app_workmode` links only `mcu gpio logger media_rtsp` (it only contains
`WorkMode.cpp` + `RtspWorkMode.cpp`). After C2 it also contains
`WorkModeRunner.cpp` = the cascade + 5 helpers, whose deps are an order of
magnitude larger. T10/T13 lesson: declare deps at the source TU; T32 link is the
gate (R7). Miss one → T32 link fails (sim may pass because sim has fatter
transitive closure via `sdk_stub`).

### 5.2 Include directories to add

Mirror the union of `main_app.cpp`'s needs (the cascade's headers) +
`app_lifecycle`'s public header:

```
${CMAKE_CURRENT_SOURCE_DIR}                       # WorkModeRunner.h, RtspWorkMode.h, WorkMode.h
${CMAKE_CURRENT_SOURCE_DIR}/..                    # app.h (path defines: MEDIA_TARGET_PATH etc.)
${CMAKE_CURRENT_SOURCE_DIR}/../app_lifecycle      # ProcessLifecycle.h
${CMAKE_CURRENT_SOURCE_DIR}/../common             # Common.h (pins)
${CMAKE_CURRENT_SOURCE_DIR}/../common/misc        # Misc (connectWifi/startDHCP/ntpSyncAndWait/...)
${CMAKE_CURRENT_SOURCE_DIR}/../common/utils       # AutoRelease.h
${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/string# StringConvert (to_string_custom/stoi_custom)
${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/crc   # utils/crc/CRC.h
${CMAKE_CURRENT_SOURCE_DIR}/../common/time/rtc    # time/rtc/RTC.h
${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf     # DeviceConfig
${CMAKE_CURRENT_SOURCE_DIR}/../config/env         # EnvManager
${CMAKE_CURRENT_SOURCE_DIR}/../config/setting     # Settings
${CMAKE_CURRENT_SOURCE_DIR}/../hardware           # PTYPE_*, INI_SECTION/KEY (app.h?), GPIO_VALUE
${CMAKE_CURRENT_SOURCE_DIR}/../hardware/daynight  # DayNightSwitch / DayNightState
${CMAKE_CURRENT_SOURCE_DIR}/../hardware/gpio      # GPIO
${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu       # MCU (syncWithMCU stays in main_app, but helpers may touch)
${CMAKE_CURRENT_SOURCE_DIR}/../hardware/power     # Power
${CMAKE_CURRENT_SOURCE_DIR}/../hardware/disk      # Disk (helpers)
${CMAKE_CURRENT_SOURCE_DIR}/../network            # MgmtServClient, StorageServClient, UsbDongle
${CMAKE_CURRENT_SOURCE_DIR}/../service/discovery  # MdnsService, MdnsParams, service::buildMdnsParams/isMdnsEnabled
${CMAKE_CURRENT_SOURCE_DIR}/../service/event      # TcpEventService
${CMAKE_CURRENT_SOURCE_DIR}/../service/http_server# http_server_*
${CMAKE_CURRENT_SOURCE_DIR}/../service/daemon     # daemon_api.h (if needed by helpers — verify)
${CMAKE_CURRENT_SOURCE_DIR}/../service/camera     # CameraFactoryConfigImporter? (only if helpers use; verify)
${CMAKE_CURRENT_SOURCE_DIR}/../storage            # MetadataDao, DatabaseManager, MediaScanner
${CMAKE_CURRENT_SOURCE_DIR}/../../media/snap      # ImageSnap, ImageSnapParams
${CMAKE_CURRENT_SOURCE_DIR}/../../media/video     # VideoRecorder, VideoParams, CameraRecorder, RecordingPostProcess
${CMAKE_CURRENT_SOURCE_DIR}/../../media/audio     # AudioRecorder, AudioParams
${CMAKE_CURRENT_SOURCE_DIR}/../../media/rtsp      # RtspServer, DEFAULT_RTSP_PORT
${CMAKE_CURRENT_SOURCE_DIR}/../../manifest        # manifest/Manifest.h
${THIRD_PARTY_PATH}/jsoncpp/include               # json/json.h
```

(The implementer must verify each header's actual include dir by grepping the
move's `#include`s against the tree; the above is the enumeration from
main_app.cpp's `:30-68` include block + the helpers' deps. Treat as the
starting checklist, not gospel.)

### 5.3 PUBLIC link libraries to add

Enumerated from the cascade + the 5 helpers' symbol use (confirmed by reading):

```
target_link_libraries(app_workmode PUBLIC
    # existing
    mcu gpio logger media_rtsp
    # NEW — cascade + helpers
    app_lifecycle              # ProcessLifecycle (ctx.lc)
    common_misc                # Misc::connectWifi/startDHCP/ntpSyncAndWait/getIPAddress/getNetworkInterfaceName/listFilenames/isJsonFile/moveFile/createDirectory/deleteFile/findUsableNetworkInterface/setNetworkInterfaceName
    common_utils_crc           # utils/crc/CRC.h (helpers)
    crc16                      # (if helpers use; verify — main_app links it)
    network                    # MgmtServClient, StorageServClient, UsbDongle
    storage                    # MetadataDao, DatabaseManager, MediaScanner
    media_snap                 # ImageSnap (processCmdSnap)
    media_recorder             # VideoRecorder, CameraRecorder, RecordingPostProcess (processCmdVideoRecord/processCmdConcurrentSnapRecord)
    audio_recorder             # AudioRecorder, AudioParams (CMD_AUDIO_RECORD/VIDEO_RECORD)
    http_server                # http_server_init/start/stop/deinit/is_running (CMD_MOBILE)
    event_service              # TcpEventService (CMD_MOBILE)
    discovery_service          # MdnsService, service::buildMdnsParams/isMdnsEnabled (CMD_MOBILE)
    setting                    # Settings (cameraMode)
    env                        # EnvManager
    devconf                    # DeviceConfig
    common_time_rtc            # RTC::getInstance() (CMD_GET_RTC/SET_RTC/NTP)
    power                      # Power (runWorkMode invalid-mode + upload)
    disk                       # Disk (helpers)
    daynight                   # DayNightSwitch / DayNightState
    jsoncpp                    # json/json.h (CMD_UPLOAD)
    civetweb                   # transitive of http_server on T32 (verify; main_app links it)
    common_utils_base64        # transitive (main_app links; verify need)
    common_utils_string        # to_string_custom/stoi_custom (verify target name)
    md5                        # transitive (main_app links; verify)
    manifest                   # manifest/Manifest.h (helpers — verify)
    pthread rt gcc stdc++
)
```

**Verification protocol for the implementer:** start from the minimal set
above, build sim, then build T32, and let the T32 **linker** name every missing
symbol → add the owning target. Do NOT trust sim alone (R7). This is the T10/T13
discipline.

### 5.4 `src/app/CMakeLists.txt`

`htc_main_app` already links `app_workmode` (sim `:90`, hw ~`:244`). Because
`app_workmode`'s new deps are PUBLIC, they propagate to `htc_main_app`
automatically — but `htc_main_app` ALSO links them directly (PRIVATE), so there
may be harmless duplication. **Do not remove `htc_main_app`'s direct links** —
after C2 `main_app.cpp` still directly uses many of them (dispatch reads
`config`, cleanupHook reads `Settings`/`Power`, tail reads `RTC`/`Misc`/`MCU`).
Leave `src/app/CMakeLists.txt` unchanged unless the T32 link proves a gap. (Risk:
`app_workmode`'s PUBLIC deps now transitively enter `htc_main_app`'s link line —
verify no symbol conflict on T32.)

---

## 6. `goto main_exit` propagation (the load-bearing decision)

### 6.1 The problem

`goto main_exit` cannot cross a function boundary. The cascade has ~30 such
gotos (`:712,716,…,1038`), each meaning "stop the cascade now, run the shutdown
tail." Once the cascade is `runCommands()`, those gotos must become `return`.

### 6.2 The decision — `return CascadeResult::Aborted`; tail runs unconditionally

- Every `goto main_exit` inside `runCommands`/`runWorkMode` →
  `return CascadeResult::Aborted;`
- Normal cascade completion (falling off the end of the last `if(command & …)`
  block) → `return CascadeResult::Completed;`
- `main()`: after `runWorkMode`/`runCommands` returns, **run the shutdown tail
  UNCONDITIONALLY** — do NOT branch on `Completed` vs `Aborted`. Why: in the
  original code, `main_exit:` runs for BOTH normal completion AND every
  `goto main_exit` (it is a single label; control reaches it either by falling
  through or by jumping). Branching on the result would CHANGE behavior (the
  "completed" path would skip the tail). So the result enum is **informational
  only** — it exists purely to replace the `goto`, not to drive control flow.

  Concretely:
  ```cpp
  // main(), after dispatch:
  WorkModeContext ctx{lc, is_rtc_work_well, mobile_rtsp_enabled, rtsp_audio_enabled,
                      argc, argv, mgmtServClient, storageServClient};
  if (is_work_mode_cmd) {
      (void)app_workmode::runWorkMode(working_mode, ctx);   // -wm
  } else {
      (void)app_workmode::runCommands(command, ctx);         // single-shot
  }
  // fall through to main_exit tail (lc.shutdown(sctx) + sim _exit / HW poweroff)
  ```
  `(void)` because the result is intentionally unused — a code comment must say
  "the original `main_exit:` is unconditional; the result only replaces `goto`."

  **Alternative considered:** make `runCommands` return `void` and just `return`
  at each ex-goto site. This works and is simpler. REJECTED only because the
  `CascadeResult` enum makes the "this was a goto" intent grep-able for the
  byte-diff reviewer (Tier-2 review) and for future C5 retirement. The
  implementer MAY use `void` if they prefer minimal surface; either is
  behavior-identical. **Recommendation: `void` is actually cleaner** — there is
  no consumer of the result. Revise to:
  ```cpp
  void runCommands(int command, WorkModeContext& ctx);
  void runWorkMode(enum workingMode mode, WorkModeContext& ctx);
  ```
  Each ex-`goto` site → bare `return;`. This is the final recommendation. (The
  `CascadeResult` enum was the first instinct; on reflection `void` is
  behavior-identical and simpler. The reviewer byte-diff still works: every
  `goto main_exit;` becomes `return;`, mechanically checkable.)

### 6.3 `runWorkMode`'s invalid-mode path (`:608-612`)

The `default:` case (`:608-612`) today does
`Power::getInstance()->requestShutdown();` (commented out!), `sleep(10);`,
`return -1;`. This is process-terminal (returns out of `main`). In `runWorkMode`
it cannot `return -1` out of `main`. Decision: `runWorkMode` performs the
`sleep(10)` then `return;` (falling through to main's shutdown tail, which on HW
does `Misc::poweroff()` — same net effect: device powers off). The
`Power::requestShutdown()` line is already commented out in the source, so no
behavior change there. **Verify** the `sleep(10)` + tail-poweroff is
observably identical to `return -1`-from-main (which skips the tail): on HW,
`return -1` from `main` → process exits → init reaps → no explicit poweroff;
whereas `sleep(10)+tail` → explicit `Misc::poweroff()`. These DIFFER. So the
invalid-mode path must NOT fall through to the tail. **Decision (revised):**
`runWorkMode` returns a `bool`/`CascadeResult` indicating "process should exit
NOW without the tail" for the invalid-mode case, and `main` honors it:
```cpp
// in runWorkMode default: case
sleep(10);
return CascadeResult::TerminalExit;   // = original "return -1" from main
// in main:
auto r = app_workmode::runWorkMode(mode, ctx);
if (r == CascadeResult::TerminalExit) return -1;   // skip the tail, exactly as today
```
So the enum IS needed after all — but only for ONE case (invalid work mode),
not for the ~30 cascade gotos (those are plain `return;` + unconditional tail).

**Final `CascadeResult`:** `{ Continue, TerminalExit }` where `Continue` = the
normal/`goto`-replaced cases (tail runs) and `TerminalExit` = the invalid-mode
early-out (tail skipped, matches today's `return -1`). `runCommands` (single-shot
path) never hits invalid-mode, so it can return `void` or `Continue`-only; for
symmetry make both return `CascadeResult`, defaulting `Continue`.

This precisely preserves the three original outcomes:
1. cascade completes normally → (today) fall to `main_exit` → (C2) `Continue` → tail runs.
2. any `goto main_exit` → (today) jump to `main_exit` → (C2) `return Continue;` → tail runs.
3. invalid work mode → (today) `return -1` (tail NOT run) → (C2) `return TerminalExit;` → `main` does `return -1` (tail NOT run).

### 6.4 The `:614-619` wrong-argc branch

Stays in dispatch (argv-shape error). It does `Power::requestShutdown()+sleep(10)+return -1`.
`return -1` from `main` is fine there (it's still in `main`, before any
`runWorkMode` call). No change needed. (Note `Power::requestShutdown()` IS
uncommented here at `:616`, unlike the `default:` case at `:610` — preserve
that asymmetry exactly.)

---

## 7. Risks (ranked, with mitigations)

- **R1 — `rtsp_singleton_used` HAL-teardown gate drift.** The cascade sets it
  via `ctx.lc.markRtspSingletonUsed()` at `:974` (CMD_MOBILE, gated by
  `mobile_rtsp_enabled`) and `:1014` (CMD_RTSP_SERVER). Must stay at the SAME
  source lines after the move. Mitigation: byte-diff must show the two
  `markRtspSingletonUsed()` calls at the corresponding positions; Tier-3 device
  A/B (next-boot-no-hang) is the decisive check. Inherited from C1; C2 only
  relocates the calls.

- **R2 — CMD_MOBILE shared by `-wm 3` (TEST_ONLY) and `-m`.** Both now flow
  through the same `runCommands`/CMD_MOBILE block. The difference is
  `mobile_rtsp_enabled` (`-m --no-rtsp` sets it false; `-wm 3` leaves it true)
  and the env sets (`-m` sets `HTC_FORCE_RECORD_DAY_MODE`/etc. before the call).
  Mitigation: `WorkModeContext.mobileRtspEnabled` carries the flag; env stays
  dispatch-side; A/B both `-wm 3` and `-m` (and `-m --no-rtsp`,
  `-m --force-day`). **Highest C2-specific risk.**

- **R3 — cleanupHook `mgmtServClient`/`storageServClient` nulling.** After C2
  these are `main()`-locals passed by ref into the ctx AND captured by ref in
  the cleanupHook closure. If the closure captures a different instance than the
  ctx holds, the null is a no-op and the clients leak/linger into shutdown.
  Mitigation: both bind to the same two `shared_ptr` locals in `main`; closure
  capture list `[&lc, &mgmtServClient, &storageServClient]` (`:634` rewrite);
  Tier-1 sim SIGTERM `-wm 2` (UPLOAD) + `-u` log-diff vs baseline.

- **R4 — sim masks teardown bugs (inherited).** Tier-3 device A/B is
  user-run; subagents cannot drive the T32. Flag every C2 deliverable with
  "device A/B pending user." This is the R1/R2 ground truth.

- **R5 — `app_workmode` link-dep growth on T32 (R7 in plan).** The new TU pulls
  ~20 libs. Sim may link fine (fat transitive closure via `sdk_stub`) while T32
  fails. Mitigation: T32 build is the gate; let the T32 linker enumerate missing
  symbols; declare each at source (§5). Do NOT declare deps only sim needs.

- **R6 — missed `ctx.`/`lc.` rewrite in the ~490-line move.** A single missed
  rewrite (e.g. `daynight_switch` left bare) = compile error (caught) OR, worse,
  resolves to a different symbol (shadowed global) = behavior drift (NOT
  caught by compile). Mitigation: the byte-diff vs `git show HEAD` MUST show
  every `static`/local→`ctx.`/`lc.` rewrite and NOTHING else; Tier-2 review
  greps the moved body for any bare `daynight_switch`/`gpio_rgb_led`/
  `program_type`/`is_rtc_work_well`/`mobile_rtsp_enabled`/`mgmtServClient`/
  `storageServClient` (all must be `ctx.*` or gone).

- **R7 — `getConfiguredPort`/`getCurrentTimeFormatted`/`processCmd*` move.** If
  any helper has a hidden dependency on a `main_app.cpp`-local symbol (another
  static, a macro from `app.h`), the move breaks. Mitigation: read each helper
  fully; grep for references to other statics; the byte-diff covers them too.

- **R8 — `workMode` enum reachability.** `runWorkMode` takes `enum workingMode`
  (from `WorkMode.h`). Ensure `WorkModeRunner.cpp` includes `WorkMode.h` (same
  dir, trivial) — but verify no ODR clash with the `static enum workingMode
  WorkMode::working_mode` member.

- **R9 — `-srtc` argv read.** `CMD_SET_RTC` reads `argv[2]` (`:693`). After C2
  the single-shot `-srtc` path calls `runCommands(CMD_SET_RTC, ctx)` with
  `ctx.argv = argv`. Verify `argv[2]` is the time string for `-srtc` invocation
  (`htc_main_app -srtc "YYYY-MM-DD HH:MM:SS"` → `argv[1]="-srtc"`, `argv[2]=time`).
  Correct. `-wm` never sets CMD_SET_RTC so `ctx.argv` is unused on the `-wm`
  path (but must still be valid — pass `argv` always).

### Tier-3 device A/B (user-run) — explicit flag
C2 is **not verifiable on sim alone** for the R1/R2/R3 teardown semantics. Each
sub-mode (`-wm 0/1/2/3/4 -rtc {0,1}`) and the single-shot set (`-m` incl.
`--no-rtsp`/`--force-day`/`--no-audio`, `-s`, `-u`, `-rs`) needs a device A/B:
baseline (`git stash` the C2 change, rebuild, run) vs C2 build. Decisive signal:
next cold boot does NOT hang in IMP `configure()` (R1). The PM must not mark C2
"verified" without it.

---

## 8. Verification plan (Tier 1-2 executor-runnable; Tier 3 user)

### Tier 1 (PC-sim, mechanical — executor/tester runs)
1. **Dual-platform build:**
   - `cmake --build build_sim -j$(nproc)` (sim)
   - `cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)` (T32) — **R7 gate**.
2. **Byte-diff the moved cascade + switch + 5 helpers** vs
   `git show HEAD:src/app/main_app.cpp`. Script:
   ```
   git show HEAD:src/app/main_app.cpp > /tmp/main_app_HEAD.cpp
   # extract :681-1170 (cascade), :584-613 (switch), :74,96,190,261,360 (helpers) from HEAD
   # extract the moved bodies from WorkModeRunner.cpp
   # diff with only these allowed rewrites:
   #   daynight_switch -> ctx.lc.daynight()
   #   gpio_rgb_led    -> ctx.lc.rgbLed()
   #   config          -> ctx.lc.config()
   #   program_type    -> ctx.lc.programType()
   #   is_rtc_work_well-> ctx.isRtcWorkWell
   #   mobile_rtsp_enabled -> ctx.mobileRtspEnabled
   #   mgmtServClient  -> ctx.mgmtServClient
   #   storageServClient -> ctx.storageServClient
   #   lc.X            -> ctx.lc.X     (lc is now ctx.lc inside runCommands)
   #   markRtspSingletonUsed() -> ctx.lc.markRtspSingletonUsed()
   #   keepRunning()   -> ctx.lc.keepRunning()
   #   waitForSignal(N)-> ctx.lc.waitForSignal(N)
   #   goto main_exit  -> return CascadeResult::Continue;
   ```
   Any logic diff (added/removed line, changed condition, changed constant) = RED.
3. **Sim smoke vs baseline:** capture baseline `app.log` + produced files +
   desc JSON for `-wm 0..4 -rtc 0`, `-wm 0..4 -rtc 1`, `-m`, `-m --no-rtsp`,
   `-s`, `-u`, `-rs` on the HEAD build; rebuild with C2; diff. Expect zero
   delta in ordered log + written artifacts.
4. **SIGTERM sim** `-wm 2` (UPLOAD, exercises R3) and `-wm 3` (MOBILE,
   exercises R1/R2): send SIGTERM, diff cleanup-log tail vs baseline.

### Tier 2 (PC-sim, semantic review — reviewer runs)
- Grep moved `WorkModeRunner.cpp` for bare (un-`ctx.`-prefixed) occurrences of
  `daynight_switch|gpio_rgb_led|program_type|is_rtc_work_well|mobile_rtsp_enabled|
  mgmtServClient|storageServClient|rtsp_singleton_used|already_in_exit_flow` —
  expect ZERO (all are `ctx.*` or gone).
- Confirm exactly two `ctx.lc.markRtspSingletonUsed()` calls, at the CMD_MOBILE
  (gated by `ctx.mobileRtspEnabled`) and CMD_RTSP_SERVER positions.
- Confirm the cleanupHook capture list includes `&mgmtServClient` and
  `&storageServClient` (R3).
- Confirm `runWorkMode`'s `default:` returns `TerminalExit` and `main` honors it
  (R6/§6.3).
- Confirm no `std::to_string`/`stoi` in the new TU (T32 uClibc).

### Tier 3 (T32 device A/B — MANDATORY, USER-RUN; not gating for subagent sign-off)
- Per-sub-mode device A/B: baseline (HEAD) vs C2. Full `app.log`, produced
  media + desc JSON, MCU regs after `syncWithMCU`, clean poweroff vs hang.
- Decisive: next cold boot does NOT hang in IMP `configure()` (R1). Run N≥3
  cold boots per sub-mode before C4.

---

## 9. Acceptance criteria (C2 done)

1. `src/app/workmode/WorkModeRunner.{h,cpp}` exist; the cascade, the `-wm`
   switch, and the 5 `-wm`-reachable static helpers live there and ONLY there
   (no duplicate in `main_app.cpp`).
2. `main_app.cpp::main()` `-wm` branch calls `runWorkMode`; single-shot flags
   call `runCommands`; the `main_exit` tail stays in `main` and runs for
   `Continue`; invalid-mode returns `TerminalExit` and `main` does `return -1`.
3. Byte-diff vs HEAD shows ONLY the §8.2 allowed rewrites — zero logic change.
4. `app_workmode` CMakeLists declares the §5.3 deps PUBLIC; both sim and T32
   link clean (T32 = R7 gate).
5. Tier-1 sim smoke (all sub-modes + single-shot) byte-identical to baseline.
6. Tier-2 grep invariants pass (no bare TU-local symbols; 2
   `markRtspSingletonUsed`; cleanupHook captures the two clients by ref).
7. Tier-3 device A/B is flagged pending-user (R4) — NOT blocking subagent
   sign-off, but explicitly listed as the decisive gate before C4.
8. No `src/hal/**` change. No new app. `-m/-s/-u/-rs/-ar/-vr/-grtc/-srtc` all
   still work (Tier-1 sim smoke covers `-ar/-vr/-grtc/-srtc` too).

---

## 10. Rollback

C2 is a pure extraction. Rollback = `git revert` the C2 commit (restores the
cascade + switch + helpers in `main_app.cpp`, removes `WorkModeRunner.{h,cpp}`,
restores the CMake). No state migration; no on-device data effect. The
`htc_main_app -wm` baseline path remains the A/B reference until C5, so a
mid-C2 rollback loses nothing operational.

---

## 11. Open question for PM (blocker: none, but flag)

The Phase C plan's C2 row says "drop dead GET_RTC/SET_RTC/AUDIO/VIDEO/HEARTBEAT"
from the moved body; the task brief says the opposite — keep the whole cascade
verbatim so the single-shot path shares it. **The brief wins** (later + more
specific, and "don't duplicate the cascade" is the explicit headline
requirement). This plan follows the brief (shape A, whole cascade). PM should
acknowledge the plan/brief divergence so the reviewer doesn't flag "kept dead
blocks" as a defect. **Not a blocker** — proceeding per the brief.
