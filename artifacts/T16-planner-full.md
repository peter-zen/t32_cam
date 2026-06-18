# T16 — Planner — Phase C-3: thin `htc_workmode_app` binary

task_id: T16
node: planner
flow: feature
plan: htc-main-app-m-w-htc-main-app-w-app-synchronous-clover.md (row C3/T16)

---

## 0. Scope recap (what is in / out)

IN (C3 only):
- Create `src/app/workmode_app.cpp` — a thin `main()` that reproduces `htc_main_app -wm <mode> -rtc <status>` by composing the already-extracted `app_lifecycle::ProcessLifecycle` + `app_workmode::runWorkMode`.
- Wire it into `src/app/CMakeLists.txt` (mirror `htc_wifi_app`): new source glob, `add_executable(htc_workmode_app ...)`, sim link block, HW link block, output-dir property.
- Minimal shared-code adjustment so the new app's `main()` can (a) produce `command` for `commonStartupPostDispatch` correctly and (b) reach `syncWithMCU()` in the HW tail (see §3/§4 — both are tiny additive exposes, no logic change to existing bodies).

OUT (do NOT do in C3):
- Do NOT repoint `media_app.cpp` spawn (`htc_main_app -wm` → `htc_workmode_app`). That is C4/T17.
- Do NOT touch `src/hal/**`.
- Do NOT change the cascade bodies, the signal machinery, or the startup sequence semantics. Only ADD the new app + the minimal exposes noted.
- Do NOT retire `htc_main_app -wm` (C5/T18).

---

## 1. The current `-wm` execution path in `main_app.cpp` (post-C2, the source of truth)

Every line ref below is the CURRENT `src/app/main_app.cpp` (HEAD = afc8da6). The new app's `main()` must reproduce this sequence, minus the single-shot dispatch branch.

### 1.1 Locals declared up-front — `main_app.cpp:168-186`
```
168  bool is_rtc_work_well = true;
169  enum workingMode working_mode = workingMode::WORKING_MODE_MAX;
175  std::shared_ptr<DeviceConfig> config;     // assigned later (= lc.config())
176  int command = CMD_HELP;                   // KEY (see §3)
183  bool mobile_rtsp_enabled = true;
184  bool rtsp_audio_enabled  = true;
185  std::shared_ptr<MgmtServClient>   mgmtServClient   = nullptr;
186  std::shared_ptr<StorageServClient> storageServClient = nullptr;
```

### 1.2 S1 path inputs → StartupConfig — `main_app.cpp:188-214`
- Sim block (`#ifdef BUILD_FOR_SIMULATION`, :190-206): computes `cfg.projectRootPath` via `Misc::getExecutablePath()` + `normalizePath("../..")`; `cfg.simRootPath` from `SIM_SD_ROOT` env or `projectRoot/sim_sdcard_runtime`; `cfg.dbPath=simRootPath+/data/db`; `cfg.mediaRoot=simRootPath+/DCIM`; `cfg.logRoot` from `SIM_LOG_DIR` env or `simRootPath/logs`; `cfg.logFile=logRoot+/app.log`; `cfg.isSimulation=true`.
- HW block (`#else`, :207-214): `EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME)`; `cfg.dbPath` from env `DB_PATH` (default `/mnt/sdcard/data/db`); `cfg.mediaRoot=/mnt/sdcard/DCIM`; `cfg.logRoot=/mnt/sdcard/logs`; `cfg.logFile=logRoot+/app.log`; `cfg.isSimulation=false`.
- `normalizePath` is a static in `main_app.cpp:53-60` (realpath-or-passthrough). The lifecycle TU has its OWN copy (`ProcessLifecycle.cpp:184-191`) — the new app needs its own copy of `normalizePath` too (it is file-local in main_app; copy the 8-line helper verbatim into `workmode_app.cpp`).

### 1.3 S1-S8 + signal install — `main_app.cpp:216-226`
```
216  app_lifecycle::ProcessLifecycle lc;
217  if (!lc.commonStartup(cfg)) { return -1; }      // S1-S8
224  if (!lc.installSignalHandlers()) { return -1; } // S8 self-pipe + signal()
```

### 1.4 The `-wm` branch (arg validation) — `main_app.cpp:233, 285-299`
```
233  const bool is_work_mode_cmd = (argv[1]=="-wm" || argv[1]=="--work-mode");
...
285  } else {   // -wm branch
289      if (argc == 5 && (argv[3]=="-rtc" || argv[3]=="--rtc-status")) {
290          working_mode = (enum workingMode)stoi_custom(argv[2]);
291          is_rtc_work_well = (bool)stoi_custom(argv[4]);
292          Logger::log(INFO, "%s working mode %d, rtc status %d", __func__, ...);
293      } else {
294          Logger::log(ERROR, "%s Invalid command %s, power off", __func__, argv[1]);
295          Power::getInstance()->requestShutdown();
296          sleep(10);
297          return -1;
298      }
299  }
```
NOTE: in the CURRENT post-C2 main_app, the `-wm` branch does NOT compute `command` here. The switch (mode→command, incl. `WORKING_MODE_TEST_ONLY→CMD_MOBILE`, RGB `asyncBlink`, invalid→return -1) moved into `app_workmode::runWorkMode` (see `WorkModeRunner.cpp:842-877`). So at this point in main_app, `command` is still `CMD_HELP`.

### 1.5 Build WorkModeContext — `main_app.cpp:307-309`
```
307  app_workmode::WorkModeContext ctx{lc, is_rtc_work_well, mobile_rtsp_enabled,
308                                    rtsp_audio_enabled, argc, argv,
309                                    mgmtServClient, storageServClient};
```
(constructed BEFORE commonStartupPostDispatch so the `goto main_exit` stays legal — but the new app uses no goto, so this ordering is a faithful copy, not a requirement.)

### 1.6 S9-S13 post-dispatch — `main_app.cpp:313-315`
```
313  if (!lc.commonStartupPostDispatch(cfg, command)) { goto main_exit; }
```
**KEY — see §3.** `command` is `CMD_HELP` for the `-wm` path in the CURRENT main_app. S11 netif selection (`ProcessLifecycle.cpp:445`) reads `program_type == PTYPE_WIFI || command == CMD_MOBILE`.

### 1.7 setCleanupHook — `main_app.cpp:323-361`
Lambda capturing `[&lc, &mgmtServClient, &storageServClient]`. Body verbatim:
- :326-330 daynight → DAY (ISP/IRLed/IRCut)
- :332-334 rgbLed → LOW
- :336-343 save Settings to `SETTING_FILE_PATH`
- :345-346 null the two client shared_ptrs (by ref)
- :348-360 SIGTERM + change-mode power-hold (HIGH on POWER_HOLD_PIN)

### 1.8 config alias — `main_app.cpp:364`
```
364  config = lc.config();   // used by the tail's config->flush()
```

### 1.9 The cascade (runWorkMode) — `main_app.cpp:375-381`
```
375  if (is_work_mode_cmd) {
376      if (app_workmode::runWorkMode(working_mode, ctx) == CascadeResult::TerminalExit) {
377          return -1;   // invalid -wm mode: skip the tail
378      }
379  } else {
380      (void)app_workmode::runCommands(command, ctx);   // single-shot path
381  }
```
For the new app, ONLY the `is_work_mode_cmd` arm applies (no single-shot).

### 1.10 Shutdown tail — `main_app.cpp:383-411`
```
383  main_exit:
392  {
393      app_lifecycle::ShutdownContext sctx;
394      sctx.programType   = lc.programType();
395      sctx.command       = command;            // KEY — see §3
396      sctx.rtcWorkedWell = is_rtc_work_well;
397      lc.shutdown(sctx);
398  }
399  #ifdef BUILD_FOR_SIMULATION
400      Logger::log(INFO, "[SIM] Program exit normally");
401      _exit(0);
402  #else
403      syncWithMCU();                 // static in main_app.cpp:62-94
404      config->flush();
405  #if POWER_MANAGER_ON
406      Misc::poweroff();
407      while(1);
408  #endif
409      return 0;
410  #endif
411  }
```
**NOTE — `POWER_MANAGER_ON` is globally `0`** (`CMakeLists.txt:61 add_definitions(-DPOWER_MANAGER_ON=0)`). So :405-408 is compiled out on BOTH platforms today. The effective HW tail is `syncWithMCU(); config->flush(); return 0;`. The new app must keep the SAME `#if POWER_MANAGER_ON` guard (do not hard-delete it) so that if the define is ever flipped back to 1, behavior matches main_app. (See §4 for where `syncWithMCU` lives.)

---

## 2. The new `src/app/workmode_app.cpp` — `main()` specification

It is the `-wm` orchestration sequence ONLY. No new logic; every block is a verbatim lift of the corresponding main_app block (with `__func__` naturally resolving to `main` in this TU — see §7 R-cosmetic).

### 2.1 File skeleton
```
#include <iostream> <fstream> <string> <unistd.h> <cstdlib> <cstring>
#include <limits.h> <json/json.h> <csignal> <queue> <thread> <chrono>
#include <mutex> <atomic> <future> <unordered_map> <algorithm>

#include "MgmtServClient.h"
#include "WorkModeRunner.h"     // app_workmode::runWorkMode + WorkModeContext + CascadeResult + CMD_*
#include "WorkMode.h"           // enum workingMode
#include "ProcessLifecycle.h"   // app_lifecycle::ProcessLifecycle + Startup/ShutdownContext
#include "DeviceConfig.h"
#include "Common.h"
#include "Logger.h"
#include "EnvManager.h"
#include "misc/Misc.h"
#include "Settings.h"
#include "MCU.h"
#include "StringConvert.h"      // stoi_custom (uClibc-safe)
#include "app.h"                // ENV_FILE_PATHNAME, POWER_HOLD_PIN, PTYPE_*, INI_*
#include "Power.h"
#include "DayNightSwitch.h"
#include "GPIO.h"               // GPIO, GPIO_VALUE, POWER_HOLD_PIN (cleanupHook body)
#include "daemon_api.h"         // (only if DAEMON_ENABLE path pulls it; lifecycle owns the real call)

using namespace network;

// Verbatim copy of main_app.cpp:53-60 (file-local; lifecycle TU has its own).
static std::string normalizePath(const std::string& path) { ... }

// syncWithMCU — see §4 (moved to a shared spot; the new app calls it there).
```

### 2.2 `main(argc, argv)` body (annotated with the main_app line it mirrors)

```
int main(int argc, char* argv[])
{
    // --- :168-186 locals (the subset the -wm path touches) ---
    bool is_rtc_work_well = true;
    enum workingMode working_mode = workingMode::WORKING_MODE_MAX;
    std::shared_ptr<DeviceConfig> config;
    int command = CMD_HELP;                       // set below BEFORE commonStartupPostDispatch (§3)
    bool mobile_rtsp_enabled = true;
    bool rtsp_audio_enabled  = true;
    std::shared_ptr<MgmtServClient>    mgmtServClient    = nullptr;
    std::shared_ptr<StorageServClient> storageServClient = nullptr;

    // --- :188-214 S1 path inputs → StartupConfig (sim vs HW, verbatim) ---
    app_lifecycle::StartupConfig cfg;
  #ifdef BUILD_FOR_SIMULATION
    cfg.isSimulation   = true;
    std::string exePath = Misc::getExecutablePath();
    cfg.projectRootPath = normalizePath(exePath + "/../..");
    std::string defaultSimRootPath = normalizePath(cfg.projectRootPath + "/sim_sdcard_runtime");
    const char* envSimRoot = std::getenv("SIM_SD_ROOT");
    cfg.simRootPath = (envSimRoot && envSimRoot[0] != '\0') ? normalizePath(envSimRoot) : defaultSimRootPath;
    const char* envLogDir = std::getenv("SIM_LOG_DIR");
    cfg.dbPath    = cfg.simRootPath + "/data/db";
    cfg.mediaRoot = cfg.simRootPath + "/DCIM";
    cfg.logRoot   = (envLogDir && envLogDir[0] != '\0') ? std::string(envLogDir) : (cfg.simRootPath + "/logs");
    cfg.logFile   = cfg.logRoot + "/app.log";
  #else
    cfg.isSimulation = false;
    EnvManager::getInstance()->parsePrimaryEnv(ENV_FILE_PATHNAME);
    cfg.dbPath    = EnvManager::getInstance()->getEnv("DB_PATH", "/mnt/sdcard/data/db");
    cfg.mediaRoot = "/mnt/sdcard/DCIM";
    cfg.logRoot   = "/mnt/sdcard/logs";
    cfg.logFile   = cfg.logRoot + "/app.log";
  #endif

    // --- :216-226 S1-S8 + signal install ---
    app_lifecycle::ProcessLifecycle lc;
    if (!lc.commonStartup(cfg))        { return -1; }
    if (!lc.installSignalHandlers())   { return -1; }

    // --- :233, 285-299 -wm arg validation (the WHOLE arg story for this app) ---
    const bool is_work_mode_cmd =
        (argc >= 2) && (std::string(argv[1]) == "-wm" || std::string(argv[1]) == "--work-mode");
    // (htc_workmode_app is -wm-only; printUsage on a non -wm argv[1] is optional —
    //  to stay byte-faithful to `htc_main_app -wm`, reject anything that isn't -wm
    //  with the same "Invalid command" poweroff/sleep/return-1 as :293-298.)
    if (is_work_mode_cmd &&
        argc == 5 && (std::string(argv[3]) == "-rtc" || std::string(argv[3]) == "--rtc-status")) {
        working_mode      = (enum workingMode)stoi_custom(argv[2]);
        is_rtc_work_well  = (bool)stoi_custom(argv[4]);
        Logger::log(LogLevel::INFO, "%s working mode %d, rtc status %d",
                   __func__, working_mode, is_rtc_work_well);
    } else {
        Logger::log(LogLevel::ERROR, "%s Invalid command %s, power off", __func__,
                    (argc >= 2 ? argv[1] : ""));
        Power::getInstance()->requestShutdown();
        sleep(10);
        return -1;
    }

    // --- §3: compute command for commonStartupPostDispatch BEFORE the call ---
    command = app_workmode::workModeToCommand(working_mode, lc);  // sets RGB asyncBlink side-effects too

    // --- :307-309 build WorkModeContext ---
    app_workmode::WorkModeContext ctx{lc, is_rtc_work_well, mobile_rtsp_enabled,
                                      rtsp_audio_enabled, argc, argv,
                                      mgmtServClient, storageServClient};

    // --- :313-315 S9-S13 post-dispatch ---
    if (!lc.commonStartupPostDispatch(cfg, command)) {
        // main_app does `goto main_exit`; the new app falls through to the
        // shutdown tail (same observable effect — the tail runs).
        goto workmode_exit;
    }

    // --- :323-361 setCleanupHook (verbatim lambda body) ---
    lc.setCleanupHook([&lc, &mgmtServClient, &storageServClient](int sig) { ... });

    // --- :364 config alias for the tail ---
    config = lc.config();

    // --- :375-378 runWorkMode (TerminalExit → skip tail) ---
    if (app_workmode::runWorkMode(working_mode, ctx) == app_workmode::CascadeResult::TerminalExit) {
        return -1;
    }

workmode_exit:
    // --- :383-411 shutdown tail ---
    {
        app_lifecycle::ShutdownContext sctx;
        sctx.programType   = lc.programType();
        sctx.command       = command;
        sctx.rtcWorkedWell = is_rtc_work_well;
        lc.shutdown(sctx);
    }
  #ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "[SIM] Program exit normally");
    _exit(0);
  #else
    syncWithMCU();                 // see §4 — shared symbol
    config->flush();
  #if POWER_MANAGER_ON
    Misc::poweroff();
    while(1);
  #endif
    return 0;
  #endif
}
```

Behavioral equivalence vs `htc_main_app -wm`:
- Arg validation identical (argc==5, argv[3]==-rtc/--rtc-status, else the poweroff/sleep/return-1).
- S1-S8, signal install, S9-S13, cleanupHook body, runWorkMode, shutdown tail — all verbatim.
- ONLY structural difference: the new app computes `command` from `mode` before commonStartupPostDispatch (§3) — which is what the PRE-C2 main_app did and the post-C2 main_app LOST. This is a behavior FIX for the new app, not a divergence from `-wm` intent. See §3.

---

## 3. DECISION: `command` for `commonStartupPostDispatch` (the key ordering question)

### 3.1 The problem
`commonStartupPostDispatch(cfg, command)` reads `command` in exactly ONE place: the HW S11 netif-selection branch (`ProcessLifecycle.cpp:445`):
```
if (program_type == PTYPE_WIFI || command == CMD_MOBILE) {
    Misc::setNetworkInterfaceName(WIFI_IFNAME);      // "wlan0"
} else if (program_type == PTYPE_ETHERNET) { ETH_IFNAME; }
...
```
The `|| command == CMD_MOBILE` clause exists precisely so that a device whose `program_type != PTYPE_WIFI` (e.g. ETH or USB-dongle sku) still brings up `wlan0` when the user runs the mobile/test mode (`-wm 3` = WORKING_MODE_TEST_ONLY → CMD_MOBILE, or `-m`).

### 3.2 The CURRENT (post-C2) regression in main_app
Pre-C2 (`git show HEAD~2:src/app/main_app.cpp`), the `-wm` branch computed `command` via the in-line switch BEFORE the inline S9-S13 block, so `-wm 3` reached netif-selection with `command == CMD_MOBILE`.
Post-C2, that switch moved into `runWorkMode` (`WorkModeRunner.cpp:842-877`), which main_app calls AFTER `commonStartupPostDispatch` (`main_app.cpp:313` vs `:376`). So the CURRENT post-C2 `htc_main_app -wm 3` reaches S11 with `command == CMD_HELP`, NOT `CMD_MOBILE`.

Net effect on HW: a non-WIFI sku running `htc_main_app -wm 3` would fall through the netif if-chain to the `else { Logger::error("program type %d not support"); return false; }` (`ProcessLifecycle.cpp:451-454`) and bail early — a REAL behavior regression vs pre-C2. (On the primary WIFI sku `program_type==PTYPE_WIFI` the first clause is taken regardless of `command`, so the regression is masked there. That masking is why sim + the common sku did not catch it.)

### 3.3 DECISION — expose `app_workmode::workModeToCommand(mode, ctx)` and call it BEFORE commonStartupPostDispatch
- Add a new non-namespace-polluting helper to the `app_workmode` API (`WorkModeRunner.h`):
  ```
  // Maps a workingMode to its CMD_* bitmap, WITH the exact side effects the
  // pre-C2 -wm switch had at the same source lines (RGB asyncBlink for
  // WORKING_MODE_UPLOAD_ONLY and WORKING_MODE_TEST_ONLY). Returns CMD_HELP for
  // an invalid mode WITHOUT sleeping/returning — the caller decides what to do
  // with CMD_HELP (runWorkMode's default case still owns the sleep+TerminalExit).
  int workModeToCommand(enum workingMode mode, WorkModeContext& ctx);
  ```
- Body = the `switch(mode)` portion of the CURRENT `runWorkMode` (`WorkModeRunner.cpp:845-874`), factored out verbatim (each case sets `command` + the same `ctx.lc.rgbLed()->asyncBlink(...)` side effects; the `default:` returns `CMD_HELP` instead of sleeping/`return TerminalExit`).
- Refactor `runWorkMode` to call it:
  ```
  CascadeResult runWorkMode(enum workingMode mode, WorkModeContext& ctx) {
      int command = workModeToCommand(mode, ctx);
      if (command == CMD_HELP) {                       // invalid mode
          Logger::log(ERROR, "%s Invalid working mode %d, power off", __func__, mode);
          sleep(10);
          return CascadeResult::TerminalExit;
      }
      return runCommands(command, ctx);
  }
  ```
  This is a PURE refactor: `runWorkMode`'s observable behavior is byte-identical (same switch, same side effects, same invalid-mode sleep+TerminalExit). The only change is the switch is now also reachable as `workModeToCommand`.
- The new `workmode_app` (and, optionally, a follow-up fix to main_app's `-wm` path — OUT OF C3 SCOPE, flagged for reviewer) calls `workModeToCommand(working_mode, ctx)` BEFORE `commonStartupPostDispatch` so S11 sees `command == CMD_MOBILE` for `-wm 3`.

### 3.4 Why this shape (alternatives rejected)
- **Reject "new app re-implements the switch"**: two copies of the mode→command map will diverge (the exact failure mode the plan warns about for the cascade). Single-source in `app_workmode` is the whole point of C2.
- **Reject "move the switch back into the callers"**: undoes C2's single-source win.
- **Reject "commonStartupPostDispatch takes workingMode instead of command"**: pollutes the lifecycle TU with work-mode vocabulary and still needs the side effects (RGB blink) which are mode-local, not lifecycle.

### 3.5 Note on the pre-existing main_app regression
This C3 task's new app will be CORRECT (command computed before post-dispatch). The latent regression in the EXISTING `htc_main_app -wm 3` (post-C2) is a separate finding. Because C3's deliverable is "the new app behaves like `htc_main_app -wm`", and the new app is strictly MORE correct than the current buggy `-wm 3` on non-WIFI skus, the planner RECOMMENDS the implementer ALSO apply the same one-line fix to main_app (`command = app_workmode::workModeToCommand(working_mode, ctx);` before `:313`, only on the `is_work_mode_cmd` arm) so the A/B baseline in Tier-3 truly matches. This is a 2-line additive change to main_app with no logic change. **Flag for reviewer approval** — it touches main_app, which the task said "do not change … logic (only ADD)". The recommendation is: add the helper + refactor runWorkMode (pure), and fix main_app's `-wm` arm to call it (restores pre-C2 behavior). If reviewer vetoes the main_app touch, the new app still stands alone correctly; only the A/B baseline for `-wm 3` on non-WIFI skus differs (and that is the pre-existing bug, not C3's).

---

## 4. DECISION: where `syncWithMCU` lives

### 4.1 Current state
`syncWithMCU()` is a `static` free function in `main_app.cpp:62-94`. Body:
```
auto devconf = DeviceConfig::getInstance();
auto mcu     = MCU::getInstance();
devconf->set(INI_SECTION_DEVICE, INI_KEY_PID,  mcu->readPID());        // if non-empty
devconf->set(INI_SECTION_SYS, INI_KEY_UPID, mcu->readUPID());          // if both non-empty
devconf->set(INI_SECTION_SYS, INI_KEY_UPWD, mcu->readUPWD());
devconf->flush();
mcu->setDatetime(localtime(now));                                     // if non-null
return true;
```
It is a thin helper over the `DeviceConfig` + `MCU` singletons — no main_app-specific state.

### 4.2 Options
- **(A) Move it into `app_lifecycle`** (e.g. a free `app_lifecycle::syncWithMCU()` in `ProcessLifecycle.cpp`, declared in `ProcessLifecycle.h`). Both apps call `app_lifecycle::syncWithMCU()`. main_app drops its static. Cleanest sharing; the lifecycle TU already links `mcu` + `devconf`.
- **(B) Move it into `app_workmode`** (`WorkModeRunner.cpp`). Fits the "work-mode stuff lives in app_workmode" theme, but `app_workmode` is conceptually the cascade, not the process tail. The HW tail is a lifecycle concern.
- **(C) Duplicate** the 30-line static in `workmode_app.cpp`. Two copies; rejected for the same divergence reason as §3.4.

### 4.3 DECISION — Option (A): `app_lifecycle::syncWithMCU()`
Rationale:
- It is process-tail work (runs after shutdown, before poweroff/return) — a lifecycle concern, not a cascade concern. Option (A) matches the C1 division of labor (transport teardown in `shutdown()`, terminal steps left to the caller — `syncWithMCU` is a terminal step the caller runs).
- The lifecycle TU already includes `DeviceConfig.h` and links `devconf`, BUT it does NOT currently link `mcu` and does NOT include `MCU.h` (verified: `src/app/app_lifecycle/CMakeLists.txt` PUBLIC link set has no `mcu`). So the move requires TWO adds: `#include "MCU.h"` in ProcessLifecycle.cpp, and `mcu` added to `app_lifecycle`'s PUBLIC `target_link_libraries`. Both are purely additive (the new symbol references `MCU::getInstance()`); they add no behavior to existing lifecycle code.
- Both apps get it from one source. main_app's `static syncWithMCU` is DELETED and its `:403` call becomes `app_lifecycle::syncWithMCU()`.

### 4.4 Spec for the move (minimal, additive to app_lifecycle)
- `ProcessLifecycle.h`: add `namespace app_lifecycle { bool syncWithMCU(); }` (free function — NOT a member; it touches only singletons, needs no `Impl`).
- `ProcessLifecycle.cpp`: paste the verbatim body of main_app:62-94 under `namespace app_lifecycle { ... }`. ADD `#include "MCU.h"` (not currently included — verified). ADD `mcu` to the PUBLIC `target_link_libraries` in `src/app/app_lifecycle/CMakeLists.txt` (not currently linked — verified). `DeviceConfig.h` is already included.
- `main_app.cpp`: delete `static bool syncWithMCU()` (:62-94); change `:403 syncWithMCU();` → `app_lifecycle::syncWithMCU();`. (This is a behavior-preserving edit to main_app; flag for reviewer — it is on the approved "minimal move" list in the task brief: "if `syncWithMCU` … must move, specify the minimal move".)
- `workmode_app.cpp`: call `app_lifecycle::syncWithMCU();` in its HW tail.

---

## 5. CMake — `src/app/CMakeLists.txt` (mirror `htc_wifi_app`)

### 5.1 Source glob + add_executable (near `:1-63`)
Add:
```
file(GLOB WORKMODE_APP_SOURCES "${SOURCES}/app/workmode_app.cpp")
...
add_executable(htc_workmode_app ${WORKMODE_APP_SOURCES})
```
(Place the `add_executable` next to `:60-63`, alongside the other apps. `include_directories` at `:12-58` already covers every header the new app needs — `workmode/`, `app_lifecycle/`, `mcu/`, `misc/`, etc. — because it was written for main_app which uses the same headers. No new include dirs.)

### 5.2 Sim link block (inside `if(BUILD_FOR_SIMULATION)`, near `:198-215`)
Minimal starting set (the app directly calls into `app_lifecycle` + `app_workmode`; both PUBLIC-link their deps, so transitives cover most). Starting set, to be pruned/augmented by the T32 link:
```
target_link_libraries(htc_workmode_app
    PRIVATE
    app_workmode
    app_lifecycle
    common_misc          # Misc::getExecutablePath/mountSDCard (via lifecycle, but main() calls getExecutablePath directly on sim)
    setting              # Settings::saveToJsonFile (cleanupHook)
    env                  # EnvManager
    devconf              # DeviceConfig (config alias)
    power                # Power::getInstance (arg-validation failure path)
    daynight             # DayNightSwitch (cleanupHook body)
    gpio                 # GPIO / POWER_HOLD_PIN (cleanupHook body)
    mcu                  # app_lifecycle::syncWithMCU
    logger
    jsoncpp              # WorkModeContext includes pull jsoncpp headers
    sdk_stub             # sim SDK stub
    pthread rt gcc stdc++
)
```
NOTE: `StringConvert.h` lives in `src/common/utils/string/` and is a HEADER-ONLY static (verified: `src/common/utils/string/CMakeLists.txt` says "StringConvert is a header-only library / No separate library needed"; `stoi_custom` is `static`-inline under `#ifdef USE_UCLIBC`). So NO `common_utils_string` link dep. The include path is already covered: `src/app/CMakeLists.txt:30` adds `${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/string` to `include_directories`, which the new `htc_workmode_app` target inherits. Just `#include "StringConvert.h"`.

### 5.3 HW link block (the top-level `else()`, near `:362-378`)
```
target_link_libraries(htc_workmode_app
    PRIVATE
    app_workmode
    app_lifecycle
    common_misc
    setting
    env
    devconf
    power
    daynight
    gpio
    mcu
    logger
    jsoncpp
    system_call          # HW SDK (replaces sdk_stub)
    imp
    alog
    pthread rt gcc stdc++
)
```
"Add as the link demands" — the implementer runs `cmake --build build -j$(nproc)` and `cmake --build build_sim -j$(nproc)` and appends any undefined-symbol lib. Given `app_lifecycle` + `app_workmode` PUBLIC-link their deps (common_misc, setting, env, devconf, logger, daynight, gpio, time/rtc, media_rtsp, http_server, event_service, discovery_service, storage, daemon, mcu, power, manifest per the C1 plan), the transitive set should cover most. The explicit list above is the directly-called-by-`main()` set as a floor.

### 5.4 Output-dir property (near `:400-403`)
```
set_target_properties(htc_workmode_app PROPERTIES
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
```
So the binary lands in `build/bin/htc_workmode_app` (HW, NFS-shared) and `build_sim/bin/htc_workmode_app` (sim), same as the other apps. (C4/T17 will spawn it from this path.)

### 5.5 DO NOT add a POST_BUILD flatten-symlinks custom command for htc_workmode_app
The `flatten_lib_symlinks.sh` POST_BUILD at `:419-422` is attached to `htc_main_app` only; it flattens `${CMAKE_BINARY_DIR}/lib` globally, so it already covers the .so files the new app links. Adding a second identical POST_BUILD to `htc_workmode_app` would race/duplicate. Leave the single one on main_app (or, if the team prefers, move it to a custom target depended-on by both — OUT OF SCOPE for C3; leave as-is).

---

## 6. Behavior-equivalence verification

### 6.1 Tier 1 — PC-sim (mechanical; PM/subagent-runnable)
- Build both platforms clean:
  - `cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)`
  - `cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)`
  - ASSERT both produce `htc_workmode_app` (in `build_sim/bin/` and `build/bin/`).
- Sim smoke, A/B per sub-mode: for `mode in 0 1 2 3 4` and `rtc in 0 1`, run
  - baseline: `build_sim/bin/htc_main_app -wm <mode> -rtc <rtc>`
  - new:      `build_sim/bin/htc_workmode_app -wm <mode> -rtc <rtc>`
  - DIFF: ordered `app.log` (modulo `__func__`: `main` appears in both since both `main()` — but the baseline log line at main_app:292/294 also says `main`; in workmode_app the same `__func__` is `main`; EXPECTED IDENTICAL), written files under `sim_sdcard_runtime/DCIM` + `media/` + desc JSON.
  - KNOWN COSMETIC DELTA: the cleanupHook/shutdown log lines that embed `__func__` differ ONLY if the function is a named static (e.g. `syncWithMCU` → still `syncWithMCU` if moved as a named fn; `signalHandler` → same). The `%s ... __func__` at the INFO/ERROR arg-validation line is `main` in both apps. Document any residual delta in the tester report.
- Invalid mode: `htc_workmode_app -wm 99 -rtc 1` → must match `htc_main_app -wm 99 -rtc 1`: the ERROR log "Invalid working mode 99, power off", `sleep(10)`, then `return -1` (TerminalExit; tail SKIPPED). Diff exit code + log.
- Bad argc: `htc_workmode_app -wm 0` (argc 3) → the `else` at :293 path: `requestShutdown` + `sleep(10)` + `return -1`. Match baseline.
- SIGTERM mid-run (modes 3 and 4, which block in `waitForSignal` loops): `kill -TERM <pid>`; diff the cleanup-log tail (daynight/LED reset, setting save) + assert `rtsp_singleton_used` gating in shutdown holds.

### 6.2 Tier 2 — PC-sim semantic review
- Confirm `workModeToCommand(mode)` returns `CMD_MOBILE` for mode 3 and that `commonStartupPostDispatch(cfg, CMD_MOBILE)` is reached BEFORE `runWorkMode` (grep the new main for the call order).
- Confirm the cleanupHook lambda is byte-identical to main_app:323-361.
- Confirm the shutdown tail constructs `ShutdownContext{programType, command, rtcWorkedWell}` identically and that `command` is the `workModeToCommand` result (so HW netif selection sees CMD_MOBILE for mode 3).
- Confirm `syncWithMCU` is called exactly once, in the HW tail, after `lc.shutdown`.

### 6.3 Tier 3 — T32 device A/B (USER-RUN, mandatory before C4/T17)
This is the decisive gate (sim masks teardown bugs — see plan §"Key hazard"). For each `mode in 0 1 2 3 4`, `rtc in 0 1`:
- baseline boot: spawn `htc_main_app -wm <mode> -rtc <rtc>`; capture full `app.log`, produced media + desc JSON, MCU regs after `syncWithMCU`, and CRITICALLY: does the NEXT cold boot hang in IMP `configure()`? (R1 symptom.)
- new boot: spawn `htc_workmode_app -wm <mode> -rtc <rtc>`; same captures.
- DIFF everything; assert next-boot-no-hang after the new app runs (this is the whole reason C3 must be device-verified before C4 repoints media_app to spawn it).
- Pay special attention to mode 3 + mode 4 on a non-WIFI sku (if available) to exercise the §3 netif-selection fix.
- The new app is UNSPAWNED in C3 (media_app still spawns `htc_main_app -wm`), so the A/B is driven manually by the user, not by the production boot path. (C4 is where the production path flips.)

### 6.4 Tier 4 — soak (post-C4, not blocking C3)
N cold boots across all 5 sub-modes after C4 repoints media_app. Noted for continuity; not a C3 gate.

---

## 7. Risks (ranked)

- **R-C3-1 (HIGHEST) — the `command`-for-commonStartupPostDispatch ordering.** If the new app calls `commonStartupPostDispatch(cfg, CMD_HELP)` for `-wm 3` (replicating the post-C2 main_app state), a non-WIFI sku fails S11 netif selection and bails. MITIGATION: §3 — expose `app_workmode::workModeToCommand(mode)` and call it before post-dispatch. This is the single most load-bearing decision in C3.
- **R-C3-2 — `syncWithMCU` placement.** If left as a main_app static, the new app can't reach it; if duplicated, divergence risk. MITIGATION: §4 — move to `app_lifecycle::syncWithMCU()` (free fn), both apps call it.
- **R-C3-3 — T32 link-dep minimal-set.** Sim links easily (sdk_stub resolves everything); T32 may surface undefined refs from transitively-pulled media/hal singletons (the cascade touches `RtspServer`, `HttpServer`, `MdnsService`, `CameraRecorder`, …). MITIGATION: start from the §5.2/§5.3 floor, run `cmake --build build`, append libs as the linker demands. The main_app HW link block (`:257-316`) is the upper bound — the new app should converge to a subset (no `storage`/`media_recorder`/`network`/`discovery_service`/`event_service`/`civetweb`/`mcu_service` unless pulled transitively). R7 from the plan.
- **R-C3-4 — the new app is UNSPAWNED in C3.** No production-path coverage until C4. The Tier-3 A/B is the only real exercise; it is user-run. If skipped, C4 inherits an unverified binary. MITIGATION: flag Tier-3 as a hard gate; do not start C4 until the user signs off the A/B.
- **R-C3-5 — cosmetic `__func__` log delta.** Both apps' `main()` logs use `__func__=="main"`, so the arg-validation lines match. Any residual delta is in helper fns; document in tester report. Not behavioral.
- **R-C3-6 — `POWER_MANAGER_ON=0` global.** The HW `Misc::poweroff()`+`while(1)` is compiled out. The new app must KEEP the `#if POWER_MANAGER_ON` guard (do not delete it) so a future flip to 1 stays byte-faithful. Low risk; just don't "clean it up".
- **R-C3-7 — `stoi_custom` uClibc safety.** `stoi_custom` (StringConvert.h) is the uClibc-safe `stoi` replacement (per MEMORY.md). The new app uses it for argv[2]/argv[4] parse. Do NOT use `std::stoi`. (Already in the spec.)
- **R-C3-8 — touching main_app / app_workmode at all.** The task said "only ADD the new app; if `syncWithMCU` or a command-mapping helper must move, specify the minimal move." The §3 `workModeToCommand` refactor of `runWorkMode` and the §4 `syncWithMCU` move both touch existing files. Both are behavior-preserving (pure refactor / verbatim move). Flagged for reviewer approval. If reviewer vetoes, fallback: the new app re-implements the switch inline (rejected, §3.4) and `syncWithMCU` inline (rejected, §4.2-C) — but the new app's correctness does NOT depend on main_app being fixed.

---

## 8. Acceptance criteria (what "done" means for C3/T16)

1. New file `src/app/workmode_app.cpp` exists, compiles on BOTH `build_sim` and `build`, produces `build_sim/bin/htc_workmode_app` + `build/bin/htc_workmode_app`.
2. `src/app/CMakeLists.txt` has the glob, `add_executable`, sim link block, HW link block, and output-dir property per §5.
3. `app_workmode::workModeToCommand(mode, ctx)` exists; `runWorkMode` refactored to call it (pure, byte-identical behavior); new app calls it BEFORE `commonStartupPostDispatch` (§3).
4. `app_lifecycle::syncWithMCU()` exists; main_app's static removed; both apps call the shared symbol in the HW tail (§4).
5. Tier-1 sim A/B passes for `mode 0..4 × rtc {0,1}` + invalid-mode + bad-argc (§6.1).
6. Tier-2 semantic review passes (§6.2).
7. Tier-3 device A/B is SCHEDULED (user-run); the new app is byte-equivalent to `htc_main_app -wm` on T32 per sub-mode, with next-boot-no-hang. (This gate may be deferred to the user; the task's own exit does not require it, but C4 MUST NOT start without it.)
8. `src/hal/**` untouched. `media_app.cpp` untouched (C4). No `std::to_string`/`std::stoi` introduced.

---

## 9. Test strategy (for the tester node)

- Unit-ish: none new (the app is a thin orchestration shell; the cascade's behavior is already covered by the existing `-wm` smoke).
- Integration (sim): the §6.1 matrix. Script it: a bash driver that runs both binaries per (mode, rtc), captures `app.log` + `find sim_sdcard_runtime -type f -newer <marker>` + exit code, and `diff -u` baseline vs new. Tolerate the documented `__func__` cosmetic delta only.
- Semantic grep (sim): assert call order in the new main (`commonStartupPostDispatch` AFTER `workModeToCommand`), assert `syncWithMCU` called once in HW tail, assert cleanupHook body byte-matches main_app.
- Device (user-run): the §6.3 A/B per sub-mode + next-boot-no-hang.
- Regression: rebuild `htc_main_app` after the §3/§4 refactors; re-run the existing `htc_main_app -wm` sim smoke to confirm no behavior change from the pure refactor.

---

## 10. Rollback

- C3 is additive: `htc_workmode_app` is unspawned (media_app still spawns `htc_main_app -wm`). Reverting C3 = delete `workmode_app.cpp`, revert the CMake lines, revert the `workModeToCommand` refactor + `syncWithMCU` move. The production boot path is unaffected at any point in C3.
- The §3 `workModeToCommand` refactor and the §4 `syncWithMCU` move are independently revertible (they are behavior-preserving; reverting them just re-inlines the switch / re-statics the helper).
- If Tier-3 surfaces a real divergence, the new app is NOT spawned (C4 not done), so no field impact — just don't start C4.

---

## 11. Evidence pointers (for the report card's verification.evidence_ref)

- `-wm` path sequence source: `src/app/main_app.cpp:166-411` (annotated in §1).
- Lifecycle API: `src/app/app_lifecycle/ProcessLifecycle.h` (StartupConfig :19-36, ShutdownContext :42-51, commonStartup/installSignalHandlers/commonStartupPostDispatch/setCleanupHook/shutdown :63-105, accessors :108-112).
- runWorkMode + the switch to factor: `src/app/workmode/WorkModeRunner.cpp:842-877`; `WorkModeRunner.h:58-63`.
- S11 netif selection reading `command == CMD_MOBILE`: `src/app/app_lifecycle/ProcessLifecycle.cpp:445`.
- Pre-C2 `-wm` switch (proof it computed command before S11): `git show HEAD~2:src/app/main_app.cpp` (the `-wm` branch with the inline switch).
- `syncWithMCU` current site: `src/app/main_app.cpp:62-94`.
- `POWER_MANAGER_ON=0`: `CMakeLists.txt:61`.
- uClibc-safe int parse: `src/common/utils/string/StringConvert.h:14` (`stoi_custom`).
- wifi_app CMake precedent: `src/app/CMakeLists.txt:63, 198-215 (sim), 362-378 (HW), 400-403 (output-dir)`.
- Netif name constants: `src/app/app.h:17-33`.
- enum workingMode: `src/app/workmode/WorkMode.h:5-11`.
