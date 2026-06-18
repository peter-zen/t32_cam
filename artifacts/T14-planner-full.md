# T14 — Phase C-1: extract `app_lifecycle` (`ProcessLifecycle`), behavior-preserving

- **Task**: T14 (Stage C-1 of Phase C, the `htc_workmode_app` extraction roadmap)
- **Scope**: C1 ONLY — extract the **process lifecycle** (signal machinery + common startup S1–S16 + shutdown tail) into a new shared lib `app_lifecycle` so both `htc_main_app` and the future `htc_workmode_app` (C3/T16) can use it. `htc_main_app` keeps its dispatch (`:796-895`) and command cascade (`:1003-1492`) **byte-identical**; only the scaffolding around them moves.
- **Non-goals (C1)**: do NOT extract the `-wm` cascade into `runWorkMode` (C2/T15); do NOT create `htc_workmode_app` (C3/T16); do NOT repoint `media_app.cpp:257` spawn string (C4/T17); do NOT touch `src/hal/**`.
- **Hard constraint**: behavior byte-identical for every mode (`-wm 0..4`, `-m`, `-s/-u/-rs/-ar/-vr/...`). Dual-platform (T32 uClibc: zero `std::to_string`/`stoi`).

All line numbers below are `git show HEAD:src/app/main_app.cpp` unless noted; HEAD in this worktree is `c8a858b` on branch `feature/new-workmode`. Verified via `Read` of `src/app/main_app.cpp` (1548 lines).

---

## 1. Goals / Non-goals

### Goals
1. New lib `src/app/app_lifecycle/` exporting `ProcessLifecycle` that owns: the self-pipe signal machinery, the S1–S16 common startup, and the shutdown tail (`main_exit` transport teardown + performCleanup split).
2. `htc_main_app` instantiates `ProcessLifecycle`; its `main()` shape becomes lifecycle-instantiation + app-local steps + **unchanged dispatch** + **unchanged cascade (refs via `lc.`)** + `lc.shutdown()`.
3. `htc_main_app -wm N` and `-m` and all single-shot flags behave byte-identically.
4. Dual-platform build (sim `build_sim/` + HW `build/`).
5. **Async-signal-safety preserved EXACTLY** — `signalHandler` keeps doing only `sig_atomic_t` store + `write(2)`, no pointer indirection in signal context.

### Non-goals
- No `runWorkMode()` extraction (C2/T15).
- No new binary (C3/T16).
- No change to the cascade body's logic — only `static`→`lc.` reference rewrites.
- No `src/hal/**` edit.

---

## 2. Impacted files

| File | Action | Why |
|---|---|---|
| `src/app/app_lifecycle/ProcessLifecycle.h` | **NEW** | Public API: `StartupConfig`, `ShutdownContext`, `ProcessLifecycle` class, accessor decls. |
| `src/app/app_lifecycle/ProcessLifecycle.cpp` | **NEW** | Verbatim move of signal machinery + S1–S16 + shutdown tail, wrapped as methods; the 4 startup helpers (`setEnvIfEmpty`/`normalizePath`/`getParentPath`/`parseMediaScannerMode`) as anonymous-namespace file-local helpers. |
| `src/app/app_lifecycle/CMakeLists.txt` | **NEW** | Mirror `src/app/workmode/CMakeLists.txt`; SHARED; PUBLIC-link the moved code's deps. |
| `src/app/main_app.cpp` | **EDIT** | Replace moved bodies with `ProcessLifecycle lc;` + `lc.` calls; dispatch + cascade unchanged in logic. Remove now-deleted statics/helpers that moved. |
| `src/app/CMakeLists.txt` | **EDIT** | `add_subdirectory(app_lifecycle)` after `:56` (`add_subdirectory(workmode)`); add `app_lifecycle` to `htc_main_app` link block in **both** sim (`:104`) and HW (`:254`). |

No other files touched. `media_app.cpp`, `daemon_app.cpp`, `wifi_app.cpp` untouched.

### Headers verified present (for the moved code's `#include`s)
`DayNightSwitch.h` (`src/hardware/daynight/`), `GPIO.h` (`src/hardware/gpio/`), `MediaScanner.h` (`src/storage/`), `TcpEventService.h` (`src/service/event/`), `MdnsService.h` (`src/service/discovery/`), `Timezone.h` (`src/common/time/timezone/`), `RTC.h` (`src/common/time/rtc/`), `daemon_api.h` (`src/service/daemon/`), `DeviceConfig.h` (`src/config/devconf/`), `EnvManager.h` (`src/config/env/`), `Settings.h` (`src/config/setting/`), `DatabaseManager.h` (`src/storage/`), `ElogInit.h` (`src/logger/`), `Power.h` (`src/hardware/power/`), `MCU.h` (`src/hardware/mcu/`), `http_server.h` (`src/service/http_server/`), `Common.h` (`src/common/`, pins `:191-204`). `AutoRelease.h` (`src/common/utils/`).

### Pins (defined in `src/common/Common.h`)
`POWER_HOLD_PIN` (`:191`), `CDS_SENSOR_PIN` (`:194`), `IR_CUT_ENABLE_PIN` (`:197`), `IR_CUT_CTRL_PIN` (`:198`), `IR_LED_PIN` (`:201`), `RGB_LED_PIN` (`:204`). Used by S5/S6 (startup) and `performCleanup` + `main_exit` (shutdown).

---

## 3. CRITICAL design decision — async-signal-safety: **Recommend (A)**

### The hazard
`signalHandler` (`:639-651`) is the ONLY function that runs in signal-delivered context. Today it does exactly two async-signal-safe ops (POSIX.1-2017 list):
1. `g_pending_signal = signal;` (a `volatile sig_atomic_t` store — `:538`)
2. `write(g_signal_pipe[1], &c, 1);` (`write(2)` is on the safe list — `:647`)

It guards on `already_in_exit_flow` (`:640`), a plain `bool`. It touches **no** `Logger`, `std::mutex`, `std::condition_variable`, `malloc`, or any object.

### Option (A) — file-scope module state in `app_lifecycle` (RECOMMENDED, SAFEST)
Keep `g_signal_pipe[2]`, `g_pending_signal`, `already_in_exit_flow`, `drainSignalPipe`, `waitForSignalOrTimeout`, `signalHandler`, `performCleanup` as **anonymous-namespace file-scope state in `ProcessLifecycle.cpp`** (NOT class members). `signalHandler` remains a free function that touches only those file-scope symbols directly — **zero pointer indirection in signal context**. `ProcessLifecycle` methods (`installSignalHandlers()`, `keepRunning()`, `waitForSignal()`) call the file-scope helpers; the signal path is unchanged from today at the machine-instruction level (same `sig_atomic_t` store + `write(2)`).

**Async-signal-safety reasoning**: in (A), the handler's data accesses are to namespace-scope objects with static storage duration. Reading `already_in_exit_flow` (a `bool`), writing `g_pending_signal` (`sig_atomic_t`), and `write()` on a static `int fd` are all async-signal-safe — these are the *exact* operations today, just relocated from one TU's file-scope to another TU's anonymous-namespace file-scope. Relocation does not change signal-context semantics. There is no `this`, no vtable, no member-offset load, no pointer the handler must dereference. This is the lowest-risk choice.

**One-binary invariant**: the plan permits at most one `ProcessLifecycle` instance per process (analogous to `DayNightSwitch::getInstance()`). Because the signal state is file-scope (singleton-by-linkage, not by-instance), two instances would share it — but C1 has exactly one instance (`htc_main_app`'s `main()`); the future `htc_workmode_app` (C3) is a *separate process* (separate binary, separate file-scope state). So "one lifecycle per binary" holds trivially. Add a debug-only assertion in the `ProcessLifecycle` constructor (guarded by `#ifndef NDEBUG` or a sim-only macro) that a static `bool g_constructed` flips from false→true exactly once, to catch accidental double-construction in tests.

### Option (B) — members + process-wide `g_active_lc` pointer (REJECTED for C1)
Move state to class members; `signalHandler` (free fn) dereferences a `ProcessLifecycle* g_active_lc` to reach members. Cleaner OO.

**Why rejected**: dereferencing a pointer + member access (offset load) in a signal handler is *pedantically* outside the async-signal-safe guarantee. It is *pragmatically* safe IF the pointer is set once before `signal()` install and never mutated — but that invariant is fragile (any future re-init / second instance silently corrupts signal context), and it adds a real instruction (`lw`/`sw` via `$gp+offset` on MIPS) the handler does not have today. The plan explicitly ranks this as RISKIER. For C1, where the goal is byte-identical behavior on the repo's most delicate code, (A) wins on every axis: identical signal-context instruction stream, no new invariants, no member-offset dereference.

**Verdict: adopt (A).** `signalHandler` stays a free function over file-scope state in `ProcessLifecycle.cpp`. This is the single highest-risk correctness point of C1, and (A) neutralizes it.

---

## 4. `ProcessLifecycle` API (`src/app/app_lifecycle/ProcessLifecycle.h`)

```cpp
#pragma once
#include <atomic>
#include <csignal>
#include <functional>
#include <memory>
#include <string>

// Forward decls — avoid pulling heavy headers into the public header.
class DeviceConfig;
namespace media { /* none needed in public API */ }
class DayNightSwitch;
class GPIO;

namespace app_lifecycle {

// S1-S16 startup configuration. skip-* flags let a pure -wm app drop the
// MediaScanner / factory-config / update-config steps that only make sense in
// the full main_app boot. htc_main_app sets none of the skip flags.
struct StartupConfig {
    // Path inputs (resolved by the caller before commonStartup):
    //   sim: projectRootPath + simRootPath (computed in main_app)
    //   hw : ENV_FILE_PATHNAME-driven (EnvManager::parsePrimaryEnv already ran)
    std::string simRootPath;          // empty on HW
    std::string projectRootPath;      // empty on HW
    std::string dbPath;               // simRootPath+"/data/db" | "/mnt/sdcard/data/db"
    std::string mediaRoot;            // simRootPath+"/DCIM"     | "/mnt/sdcard/DCIM"
    std::string logRoot;              // simRootPath+"/logs"     | "/mnt/sdcard/logs"
    std::string logFile;              // logRoot + "/app.log"
    bool isSimulation = false;        // mirrors BUILD_FOR_SIMULATION

    // Skip flags (default false = run the step; htc_main_app leaves all false):
    bool skipMediaScanner   = false;  // S3
    bool skipFactoryConfig  = false;  // HW-only S13
    bool skipUpdateConfig   = false;  // HW-only S14
    bool skipDaemonRegister = false;  // S10 (gated by DAEMON_ENABLE in main_app today)
    bool skipSDMount        = false;  // S12 (netif selection depends on it)
};

// Shutdown context: caller-provided out-params + the flags the cascade sets.
// The cascade (which stays in main_app) sets programType / command / rtcStatus
// before calling shutdown(); shutdown() reads them to decide the HW poweroff
// netif path and the rtsp_singleton_used gate.
struct ShutdownContext {
    int  programType      = 0;        // config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, ...)
    int  command          = 0;        // CMD_* bitmask (dispatch result)
    bool rtcWorkedWell    = true;     // is_rtc_work_well
    bool rtspSingletonUsed = false;   // set by CMD_MOBILE / CMD_RTSP_SERVER
    bool mobileRtspEnabled = true;    // -m mode default
    // Callback the caller wires to its mode-local resets (daynight/LED/setting-save
    // duplication that performCleanup owns). See §6 performCleanup hook.
    std::function<void(int)> cleanupHook;
};

class ProcessLifecycle {
public:
    ProcessLifecycle();
    ~ProcessLifecycle();

    // S1-S16 common startup. Performs: env/DB/MediaScanner/EasyLogger/DayNight/
    // RGB-LED/AutoRelease/signal-pipe/daemon-registration/Settings/DeviceConfig+
    // program_type/SD-mount+netif/factory-config/update-config/timezone.
    // Returns false on a fatal step that requires the caller to bail (mountSDCard
    // failure, factory import failure, update-config applied) — caller then does
    // `goto main_exit` semantics via lc.shutdown().
    bool commonStartup(const StartupConfig& cfg);

    // Create the self-pipe (O_NONBLOCK both ends) + signal(SIGINT/SIGTERM, signalHandler).
    // MUST be called after commonStartup (the pipe is created here, matching
    // today's ordering: pipe created at :780, handlers registered at :793).
    void installSignalHandlers();

    // Returns !already_in_exit_flow (the run-loop predicate).
    bool keepRunning() const;

    // = waitForSignalOrTimeout(timeoutMs) today. Side effect: on first signal,
    // sets already_in_exit_flow and invokes the cleanupHook. Returns the signal
    // number or 0 on timeout.
    int  waitForSignal(int timeoutMs);

    // Caller registers its mode-specific resets (today the body of performCleanup
    // :589-632 minus the transport teardown). Invoked once, on the main thread,
    // the first time waitForSignal() catches a signal.
    void setCleanupHook(std::function<void(int)> hook);

    // Set by CMD_MOBILE (:1296) / CMD_RTSP_SERVER (:1336) callers at the SAME
    // source lines — gates RtspServer::getInstance()->shutdown() in shutdown().
    void markRtspSingletonUsed();

    // The main_exit tail: TcpEvent/Mdns stop, the rtsp_singleton_used-gated
    // RtspServer::getInstance()->shutdown() (the "before the freeze" gate,
    // :1504-1512), self-pipe close, Settings save, RGB-LED/auto_release reset.
    // Does NOT do the final sim _exit / HW syncWithMCU+poweroff — the caller
    // runs those (see §7 rewiring) because they are terminal and app-specific.
    void shutdown(ShutdownContext& ctx);

    // --- Accessors used by the cascade (refs rewritten to lc.) ---
    std::shared_ptr<DayNightSwitch> daynight() const;   // daynight_switch
    std::shared_ptr<GPIO>           rgbLed()   const;   // gpio_rgb_led
    std::shared_ptr<DeviceConfig>   config()   const;   // DeviceConfig::getInstance()
    int                             programType() const; // set in commonStartup (S11)
    const std::string&              settingFilePath() const; // SETTING_FILE_PATH (S9)

    ProcessLifecycle(const ProcessLifecycle&) = delete;
    ProcessLifecycle& operator=(const ProcessLifecycle&) = delete;
};

}  // namespace app_lifecycle
```

### Notes on the API
- **`signalHandler`, `g_signal_pipe`, `g_pending_signal`, `already_in_exit_flow`, `drainSignalPipe`, `waitForSignalOrTimeout`** are **NOT** class members — they stay as anonymous-namespace file-scope symbols in `ProcessLifecycle.cpp` (option A). `installSignalHandlers/keepRunning/waitForSignal` are thin wrappers around them.
- **`programType()`** accessor returns the value captured during `commonStartup` S11 (`config->get(INI_SECTION_BOOT, INI_KEY_PTYPE, ...)`), so the cascade can keep reading `program_type` without re-querying. (Today `program_type` is a `main()` local at `:913`; the cascade reads it from there. After extraction, the cascade reads `lc.programType()`.)
- **`config()`** returns `DeviceConfig::getInstance()` (the `config` local at `:912`). It's a singleton accessor anyway; exposing it keeps cascade refs as `lc.config()->...`.
- **`settingFilePath()`** returns the S9-computed string (`:908`). Cascade reads it once at `:1525` (`Settings::getInstance()->saveToJsonFile(setting_file_path)`); that line is inside `shutdown()` so the accessor is mainly for `performCleanup` (which also reads SETTING_FILE_PATH at `:602`) — but `shutdown()` will call it internally; the public accessor is for parity/future C2 use.

---

## 5. `ProcessLifecycle.cpp` — what moves in, verbatim

### File-scope anonymous-namespace state (option A) — verbatim from `main_app.cpp`
| Today's symbol (line) | New home | Form |
|---|---|---|
| `already_in_exit_flow` (`:522`) | anon-ns in `ProcessLifecycle.cpp` | `bool` (keep plain `bool`; only `g_pending_signal` needs `sig_atomic_t` — matching today exactly) |
| `g_signal_pipe[2]` (`:537`) | anon-ns | `int[2] = {-1,-1}` |
| `g_pending_signal` (`:538`) | anon-ns | `volatile sig_atomic_t` |
| `drainSignalPipe()` (`:544`) | anon-ns free fn | verbatim |
| `waitForSignalOrTimeout(int)` (`:560`) | anon-ns free fn | verbatim; `ProcessLifecycle::waitForSignal` delegates to it |
| `signalHandler(int)` (`:639`) | **anon-ns free fn, registered by `installSignalHandlers`** | verbatim |
| `performCleanup(int)` (`:589`) | **split**: transport teardown moves into `shutdown()`; the mode-local resets (daynight/LED/setting-save) become the **caller-registered cleanupHook** (see §6). | see §6 |

### Startup helpers moved into `ProcessLifecycle.cpp` anonymous namespace (verbatim)
| Helper (line) | Used by | Decision |
|---|---|---|
| `setEnvIfEmpty` (`:105`) | S1 (5 uses in `:670-675`) | **MOVE** (anon-ns helper, sim-only path) |
| `normalizePath` (`:114`) | S1 (3 uses `:662-667`) | **MOVE** |
| `getParentPath` (`:123`) | S3 (1 use `:709`) | **MOVE** |
| `parseMediaScannerMode` (`:144`) | S3 (1 use `:706`) | **MOVE** |
| `trimConfigString` (`:86`) | used by `parseMediaScannerMode` (`:146`) | **MOVE** (only consumer is `parseMediaScannerMode`) |
| `getConfiguredPort` (`:154`) | cascade CMD_MOBILE (`:1231`)/RTSP (`:1335`) | **STAY in main_app** (used by dispatch+cascade, not by lifecycle) |
| `getCurrentTimeFormatted` (`:75`) | snap/video helpers (`:273,337,410,446,479` etc.) | **STAY in main_app** (only used by `processCmd*` helpers that stay) |
| `syncWithMCU` (`:166`) | `main_exit` HW branch (`:1540`) | **MOVE into `shutdown()`** (HW-only transport sync) |
| `processCmdSnap` / `processCmdVideoRecord` / `processCmdConcurrentSnapRecord` (`:248/319/418`) | cascade CMD_SNAP/VIDEO_RECORD | **STAY in main_app** (C2 candidates) |
| `parseIniFile` (`:201`) | (grep shows no caller in the moved regions) | **STAY** (verify with implementer; appears dead — do not delete in C1) |
| `printUsage` (`:486`) | dispatch (`:798,851`) | **STAY in main_app** |

### `commonStartup()` body — verbatim S1–S16, mapped to lines
```
S1  env bootstrap          :659-678   (sim: SIM_SD_ROOT + setEnvIfEmpty x5;
                                       hw: EnvManager::parsePrimaryEnv)
S2  Database init          :680-698   (db_path + DatabaseManager::init)
S3  MediaScanner           :700-716   (gated by skipMediaScanner)
S4  EasyLogger             :718-750   (sim/hw elog_init_with_config)
S5  DayNightSwitch         :752-757   (setCdsPins/setIRLedPins/setIRCutPins)
S6  RGB-LED GPIO           :759-763   (exportGPIO/setDirection; nullptr on fail)
S7  AutoRelease            :765-776   (the reset lambda — see §6 for rewire)
S8  self-pipe + signal     :778-794   (pipe + fcntl O_NONBLOCK + signal install)
S9  Settings load          :908-911   (SETTING_FILE_PATH -> loadFromJsonFile)
S10 DeviceConfig + ptype   :912-914   (config->get(BOOT/PTYPE); lc.programType_ set)
S11 SD mount + netif       :915-944   (sim/hw branch; sim netif at :922-927,
                                       hw netif at :935-944; factory-config
                                       :946-970 + update-config :972-992 are
                                       HW-only sub-steps here)
S12 daemon register        :897-906   (gated by DAEMON_ENABLE + skipDaemonRegister)
S13 timezone               :993-998   (HW-only, inside the HW `#else` block)
```
- **Re-sequencing**: today the source ordering is S1-S8 (in `main()` before dispatch `:796`) then S9-S13 (after dispatch `:897-998`). `commonStartup()` takes them **as one block in source order** BUT must preserve the original call ordering exactly: S1→S2→S3→S4→S5→S6→S7→S8, then S9→S10→S11→S12→S13. The only wrinkle: today S8 (signal install `:778-794`) sits BEFORE dispatch, and S9-S13 sit AFTER dispatch. We have two faithful options:
  - **(preferred)** `commonStartup()` runs S1-S8 only; the caller runs dispatch (`:796-895`) between `commonStartup()` and a second lifecycle call `commonStartupPostDispatch()` that runs S9-S13. This preserves source ordering precisely and keeps dispatch in `main()` exactly where it is.
  - (alternative) fold S9-S13 into `commonStartup()` and move dispatch to after `commonStartup()` — but that re-orders dispatch relative to S8 (signal handlers installed before dispatch), which is actually the SAME as today (signal handlers at `:793` are already before dispatch at `:796`). Re-reading: today signal handlers are installed at `:793`, dispatch at `:796` — both before S9-S13 (`:897-998`). So a single `commonStartup()` that runs S1-S8 then has dispatch interleave then S9-S13 is NOT a single call. **Use the preferred split: `commonStartup()` (S1-S8) + `commonStartupPostDispatch()` (S9-S13)**, with dispatch in between. This is the only way to keep dispatch in `main()` at its exact current position. Update the `ProcessLifecycle.h` to add `commonStartupPostDispatch(const StartupConfig&)` — same `StartupConfig` (skip flags used in S10-S13).
- **`AutoRelease` (S7, `:765-776`)**: the reset lambda reads `daynight_switch`/`gpio_rgb_led`. Two faithful choices:
  - **(preferred)** the lifecycle OWNS the `AutoRelease` instance as a member (`AutoRelease auto_release_`), and the lambda reads the lifecycle's members. The lambda body becomes:
    ```cpp
    if (auto dn = daynight_switch_) { dn->controlISP(Day); dn->controlIRCut(Day); dn->controlIRLed(Day); }
    if (auto led = gpio_rgb_led_)   { led->setConstant(GPIO_VALUE::LOW); }
    ```
    This is byte-identical to today's `:765-776` body (same calls, same order, same guards). The `auto_release.release()` call at `:1535` (main_exit) becomes `lc.releaseAutoRelease()` (a new tiny method) or is folded into `shutdown()` — **fold it into `shutdown()`** so the main_exit tail is one call. `shutdown()` calls `auto_release_.release()` at the same point relative to the poweroff tail as today (`:1535` is after the power-hold GPIO write at `:1530-1533`, before `_exit`/`syncWithMCU`).

### `installSignalHandlers()` body — verbatim `:778-794`
The `pipe(g_signal_pipe)` + two `fcntl(O_NONBLOCK)` + `signal(SIGINT/SIGTERM, signalHandler)`. Today this is one block at `:778-794` inside `main()`. Move into the method verbatim, referencing the anon-ns `g_signal_pipe`/`signalHandler`.

> **Ordering invariant (Tier-2 check)**: `commonStartup()` must run S8's `pipe()`+handler-install — i.e. today `installSignalHandlers()` is called after `commonStartup()` and BEFORE dispatch. Keep `lc.commonStartup(cfg); lc.installSignalHandlers();` as consecutive calls in `main()`; dispatch follows. This matches today's `:778-794` (pipe+handlers) immediately preceding `:796` (dispatch). ✅

---

## 6. The `performCleanup` → cleanupHook split (Risk R3)

Today `performCleanup(int sig)` (`:589-632`) does TWO categories of work, run as one function on the main thread when `waitForSignalOrTimeout` catches a signal (`:572-575`):

| Lines | Category | New home |
|---|---|---|
| `:590` Logger | log | (mode-local) → **cleanupHook** |
| `:592-596` daynight DAY reset | mode-local reset | → **cleanupHook** |
| `:598-600` RGB-LED LOW | mode-local reset | → **cleanupHook** |
| `:602-609` Settings save to SETTING_FILE_PATH | mode-local reset | → **cleanupHook** |
| `:611-614` http_server stop+deinit | transport teardown | → **stays in lifecycle** (move into a private `transportTeardown_()` called from BOTH `shutdown()` AND — wait: see below) |
| `:615` TcpEventService::stop | transport teardown | → lifecycle |
| `:616-617` mgmtServClient/storageServClient = nullptr | **caller state** (these are main_app locals today, `:531-532`) | → **cleanupHook** (they become mode-local in C2; in C1 the hook sets them to nullptr) |
| `:619-631` SIGTERM power-hold (change-mode) | mode-local | → **cleanupHook** |

**The split that preserves today's behavior EXACTLY:**

`waitForSignal()` (the lifecycle method) on first signal:
1. sets `already_in_exit_flow = true`
2. invokes the **cleanupHook**(sig) — the caller-registered lambda that contains the `:590-609` + `:616-617` + `:619-631` body (mode-local resets + the two `= nullptr` + the SIGTERM power-hold).

The transport teardown (`:611-615` http_server/TcpEvent) is NOT in the hook — it stays in the **lifecycle** and runs in `shutdown()` (the main_exit tail already calls `TcpEventService::getInstance()->stop()` at `:1501` and the http_server stop is idempotent). Today `performCleanup` stops them at signal-catch AND `main_exit` stops TcpEvent again (`:1501`) — both are idempotent `stop()`. To be byte-faithful to *ordering*, `shutdown()` calls them at the main_exit point (`:1501-1502`); the hook does NOT duplicate them (avoiding a double-stop that, while idempotent today, is not byte-faithful in log output). **Net: transport teardown lives only in `shutdown()`; mode-local resets live only in the hook.** This is cleaner than today and log-identical for the normal (non-signal) exit path. For the signal path, today's `performCleanup` logs "Processing signal %d" then does resets + transport; the hook logs the same line + does resets, and the transport stop happens at main_exit (which runs immediately after in the signal path too, since `waitForSignalOrTimeout` returning causes the run-loop to break and fall through to `main_exit`). **Verify the log ordering in Tier 2.**

The `main_app` registers the hook after `commonStartup`:
```cpp
lc.setCleanupHook([&](int sig) {
    Logger::log(LogLevel::INFO, "Processing signal %d on main thread", sig);
    if (auto dn = lc.daynight()) { dn->controlISP(Day); dn->controlIRCut(Day); dn->controlIRLed(Day); }
    if (auto led = lc.rgbLed())  { led->setConstant(GPIO_VALUE::LOW); }
    const auto& sfp = lc.settingFilePath();
    if (sfp.empty()) { Logger::log(LogLevel::ERROR, "Failed to get setting file path"); }
    else if (!Settings::getInstance()->saveToJsonFile(sfp)) { Logger::log(LogLevel::ERROR, "Failed to save setting file: %s", sfp.c_str()); }
    mgmtServClient = nullptr;        // main_app locals
    storageServClient = nullptr;
    if (sig == SIGTERM) { /* :619-631 power-hold block verbatim */ }
});
```
The body is `:590-609` + `:616-631` verbatim (minus `:611-615` which moved to shutdown). `mgmtServClient`/`storageServClient` remain `main_app` locals (they move to `WorkModeContext` only in C2).

---

## 7. Full static-handling table (every TU-private symbol)

| Symbol (today) | Line | Category | New home |
|---|---|---|---|
| `already_in_exit_flow` | `:522` | signal flow flag | **anon-ns file-scope** in `ProcessLifecycle.cpp` (option A) |
| `rtsp_audio_enabled` | `:523` | -rs/-m audio flag | **STAYS in main_app** (only read by the `-rs`/`-m` dispatch `:828,843`; not touched by lifecycle) |
| `mobile_rtsp_enabled` | `:524` | -m rtsp flag | **STAYS in main_app** (dispatch `:826` + cascade `:1295,1322`); moves to WorkModeContext in C2 |
| `rtsp_singleton_used` | `:530` | HAL teardown gate | **lifecycle member** `rtspSingletonUsed_` + `markRtspSingletonUsed()`. **Set ONLY by callers at the SAME source lines**: CMD_MOBILE `:1296` and CMD_RTSP_SERVER `:1336` call `lc.markRtspSingletonUsed()`. `shutdown()` gates `RtspServer::getInstance()->shutdown()` on it (`:1510-1512`). **(R1)** |
| `mgmtServClient` | `:531` | AUTH/UPLOAD client | **STAYS in main_app** (main_app local); set to nullptr in cleanupHook; C2 moves to WorkModeContext |
| `storageServClient` | `:532` | UPLOAD client | **STAYS in main_app** |
| `g_signal_pipe[2]` | `:537` | self-pipe | **anon-ns file-scope** (option A) |
| `g_pending_signal` | `:538` | sig_atomic_t | **anon-ns file-scope** |
| `drainSignalPipe()` | `:544` | pipe drain | **anon-ns free fn** |
| `waitForSignalOrTimeout()` | `:560` | poll loop | **anon-ns free fn**; `ProcessLifecycle::waitForSignal` delegates |
| `performCleanup()` | `:589` | signal cleanup | **SPLIT**: transport→`shutdown()`; resets→caller `cleanupHook` (§6) |
| `signalHandler()` | `:639` | signal context | **anon-ns free fn** (option A) — registered by `installSignalHandlers()` |
| `daynight_switch` | `:72` | DayNight singleton | **lifecycle member** `daynight_switch_` + `daynight()` accessor |
| `gpio_rgb_led` | `:73` | RGB LED GPIO | **lifecycle member** `gpio_rgb_led_` + `rgbLed()` accessor |
| `auto_release` | `:765` (local) | RAII reset | **lifecycle member** `auto_release_`; `.release()` called in `shutdown()` |
| `setting_file_path` | `:908` (local) | settings json path | **lifecycle member** `setting_file_path_` + `settingFilePath()` accessor |
| `config` | `:912` (local) | DeviceConfig | **lifecycle** (wraps `DeviceConfig::getInstance()`); `config()` accessor |
| `program_type` | `:913` (local) | ptype | **lifecycle member** `programType_` + `programType()` accessor; set in S10 |
| `timezone` | `:655` (local) | tz string | local to `commonStartupPostDispatch` S13 (HW-only) — not exposed |
| `update_config_exists` | `:656` (local) | update cfg flag | local to S14 (HW-only) |
| `is_rtc_work_well` | `:657` (local) | rtc status | **STAYS in main_app** (parsed from `-rtc` arg `:857`; read by cascade CMD_SNAP); passed into `ShutdownContext.rtcWorkedWell` |
| `working_mode` | `:658` (local) | -wm mode | **STAYS in main_app** (dispatch only) |

---

## 8. `htc_main_app` rewiring — the new `main()` shape

```cpp
int main(int argc, char* argv[]) {
    // --- S1 path inputs (sim only) computed here, identical to :659-678 ---
    // (simRootPath / projectRootPath / db_path / media_root / log_root / log_file)
    // These stay in main() because they depend on getExecutablePath/SIM_SD_ROOT
    // and feed StartupConfig. On HW, parsePrimaryEnv runs here too.
    app_lifecycle::StartupConfig cfg;
    cfg.isSimulation =
#ifdef BUILD_FOR_SIMULATION
        true;
    cfg.simRootPath     = simRootPath;
    cfg.projectRootPath = projectRootPath;
    cfg.dbPath     = db_path;
    cfg.mediaRoot  = media_root;
    cfg.logRoot    = log_root;
    cfg.logFile    = log_file;
#else
        false;
    cfg.dbPath    = EnvManager::getInstance()->getEnv("DB_PATH", "/mnt/sdcard/data/db");
    cfg.mediaRoot = "/mnt/sdcard/DCIM";
    cfg.logRoot   = "/mnt/sdcard/logs";
    cfg.logFile   = cfg.logRoot + "/app.log";
#endif
    // skip flags all false for htc_main_app.

    app_lifecycle::ProcessLifecycle lc;
    if (!lc.commonStartup(cfg)) {        // S1-S8
        goto main_exit;                   // mountSDCard etc. failure path preserved
    }
    lc.installSignalHandlers();          // pipe + signal()  (:778-794)

    // ---- unchanged dispatch :796-895 ----
    int command = CMD_HELP;
    // ... (identical arg-parsing) ...
    // REWRITES inside dispatch (the only lifecycle refs in dispatch):
    //   :865,871  gpio_rgb_led->asyncBlink(N)  -> lc.rgbLed()->asyncBlink(N)
    //   :826      mobile_rtsp_enabled = false  -> (stays main_app local; unchanged)
    //   :828,843  rtsp_audio_enabled = false    -> (stays main_app local; unchanged)

    if (!lc.commonStartupPostDispatch(cfg)) {  // S9-S13 (Settings/DeviceConfig/
                                               // ptype/SD-mount+netif/factory/
                                               // update-config/timezone)
        goto main_exit;
    }

    // ---- unchanged cascade :1003-1492, refs rewritten via lc. ----
    // (see §9 for the full ref-rewrite list)

main_exit:
    app_lifecycle::ShutdownContext sctx;
    sctx.programType       = lc.programType();
    sctx.command           = command;
    sctx.rtcWorkedWell     = is_rtc_work_well;
    sctx.rtspSingletonUsed = lc.rtspSingletonUsed();   // accessor for R1 gate
    lc.shutdown(sctx);                                  // TcpEvent/Mdns/RTSP-gate/
                                                       // pipe-close/Settings-save/
                                                       // auto_release.release()
#ifdef BUILD_FOR_SIMULATION
    Logger::log(LogLevel::INFO, "[SIM] Program exit normally");
    _exit(0);
#else
    syncWithMCU();        // stays in shutdown() actually — see note
    lc.config()->flush(); // = config->flush() at :1541
#if POWER_MANAGER_ON
    Misc::poweroff();
    while(1);
#endif
    return 0;
#endif
}
```

### Shutdown-tail ordering detail (R3)
Today `main_exit` (`:1494-1548`) runs, in order:
1. `TcpEventService::stop` + `MdnsService::stop` (`:1501-1502`)
2. rtsp_singleton_used-gated `RtspServer::getInstance()->shutdown()` (`:1510-1512`) ← **R1 gate, before the freeze**
3. close self-pipe both ends (`:1516-1523`)
4. `Settings::saveToJsonFile(setting_file_path)` (`:1525`)
5. "Power off From Main function" log (`:1527`)
6. HW: power-hold GPIO LOW (`:1529-1533`)
7. `auto_release.release()` (`:1535`)
8. sim: `_exit(0)`; HW: `syncWithMCU` (`:1540`) + `config->flush()` (`:1541`) + `Misc::poweroff()`/`while(1)` (`:1543-1544`)

`shutdown()` owns steps 1-7 verbatim (in this order). Step 8 is terminal and app-specific: **keep `_exit(0)` / `syncWithMCU` + `config->flush()` + `poweroff()` in `main()`** (after `lc.shutdown()` returns), because `_exit`/`poweroff` are process-terminal and `syncWithMCU` is HW-only — folding them in would make `shutdown()` non-returning on HW, which complicates the sim path. `syncWithMCU` stays as a main_app static (it's a one-line helper over MCU/DeviceConfig singletons) — it does NOT need to be a lifecycle method. **Correction to §5 table**: `syncWithMCu` **STAYS in main_app** (called only from the HW tail `:1540`); do not move it. This keeps the lifecycle free of the MCU-writeback concern (which is a shutdown-side-effect, not a lifecycle concern).

### `setting_file_path` at `:1525`
This reads the `main()` local `setting_file_path` (`:908`). After extraction that value lives in `lc.settingFilePath()`. `shutdown()` calls `Settings::getInstance()->saveToJsonFile(setting_file_path_)` internally (step 4) — so the `:1525` line moves INTO `shutdown()`. The `main()` no longer references `setting_file_path` directly.

---

## 9. Cascade ref-rewrite list (lines `:1003-1492`, logic UNCHANGED)

Every lifecycle-owned reference in the cascade + dispatch rewrites `lc.`. Plain locals (`command`, `config`(→`lc.config()`), `program_type`(→`lc.programType()`), `is_rtc_work_well`, `mgmtServClient`, `storageServClient`, `mobile_rtsp_enabled`, `rtsp_audio_enabled`) that STAY in main_app are unchanged.

| Line(s) | Today | Rewrite |
|---|---|---|
| `:865,871` (dispatch) | `gpio_rgb_led->asyncBlink(N)` | `lc.rgbLed()->asyncBlink(N)` |
| `:1062,1063,1093` | `config->get(...)` | `lc.config()->get(...)` |
| `:1296` | `rtsp_singleton_used = true;` | `lc.markRtspSingletonUsed();` |
| `:1216-1228` | `daynight_switch->...` | `lc.daynight()->...` |
| `:1315` | `while (!already_in_exit_flow)` | `while (lc.keepRunning())` |
| `:1316` | `waitForSignalOrTimeout(1000)` | `lc.waitForSignal(1000)` |
| `:1336` | `rtsp_singleton_used = true;` | `lc.markRtspSingletonUsed();` |
| `:1337-1339` | `runRtspServerUntilSignal(port, []{return !already_in_exit_flow;}, [](int ms){waitForSignalOrTimeout(ms);})` | `runRtspServerUntilSignal(port, [&lc]{return lc.keepRunning();}, [&lc](int ms){lc.waitForSignal(ms);})` |
| `:1373-1375` | `gpio_rgb_led->setConstant(HIGH)` | `lc.rgbLed()->setConstant(HIGH)` |
| `:1397` | `DeviceConfig::getInstance()->get(...)` | unchanged (singleton accessor; or `lc.config()->get(...)` — prefer `lc.config()` for consistency) |

`config` local (`:912`) is replaced throughout the cascade by `lc.config()`. The cleanest implementation: keep a local `auto config = lc.config();` at the top of the post-dispatch region (S10 in `commonStartupPostDispatch` already captures it; main_app can keep a local alias `auto& config = /* lc.config() */` for the cascade to minimize diff). **Implementer choice**: either rewrite every `config->` to `lc.config()->` (larger diff, clearer ownership) or keep a `auto config = lc.config();` local (smaller diff). Recommend the local alias for minimal diff = easier byte-diff review.

---

## 10. CMake

### `src/app/app_lifecycle/CMakeLists.txt` (NEW — mirror `src/app/workmode/CMakeLists.txt`)
```cmake
file(GLOB SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../logger
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/env
        ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/daynight
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/gpio
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu
        ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/power
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/rtc
        ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/timezone
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/daemon
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/discovery
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/event
        ${CMAKE_CURRENT_SOURCE_DIR}/../service/http_server
        ${CMAKE_CURRENT_SOURCE_DIR}/../storage
        ${CMAKE_CURRENT_SOURCE_DIR}/../media/rtsp
        ${CMAKE_CURRENT_SOURCE_DIR}/../common
        ${CMAKE_CURRENT_SOURCE_DIR}/../../common         # Common.h (pins)
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils             # AutoRelease.h path if needed
)

add_library(app_lifecycle SHARED ${SOURCE_FILES})

target_link_libraries(app_lifecycle PUBLIC
        common_misc        # Misc (mountSDCard/createDirectory/etc.) + ntpSyncAndWait
        setting            # Settings
        env                # EnvManager
        devconf            # DeviceConfig
        logger             # Logger + ElogInit + elog_i
        daynight           # DayNightSwitch
        gpio               # GPIO
        mcu                # MCU (syncWithMCU stays in main_app; but daynight/power may pull)
        power              # Power::getInstance()->isChangeModeRequested()
        common_time_rtc    # RTC (S-getRTC is in cascade, but RTC not in startup — keep for safety? NO: RTC is cascade-only, do NOT link here)
        common_time_timezone  # Timezone (S13)
        http_server        # http_server_is_running/stop/deinit (performCleanup transport + shutdown)
        event_service      # TcpEventService (shutdown transport)
        discovery_service  # MdnsService (shutdown transport)
        storage            # MediaScanner (S3) + DatabaseManager (S2)
        media_rtsp         # RtspServer::getInstance()->shutdown() (R1 gate in shutdown)
        daemon             # registerToDaemonServer (S12)
        jsoncpp            # Json (factory-config importer uses it)
        pthread rt gcc stdc++
)

set_target_properties(app_lifecycle PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
```
**Dep audit note for implementer**: the moved code transitively needs — verify at link time and prune over-linking:
- `media_rtsp` — **required** (`RtspServer::getInstance()->shutdown()` in `shutdown()`, R1).
- `http_server`/`event_service`/`discovery_service` — **required** (transport teardown in `shutdown()`).
- `storage` — **required** (`MediaScanner` S3, `DatabaseManager` S2).
- `daemon` — **required** only if `DAEMON_ENABLE` and `skipDaemonRegister` false (S12); link unconditionally for simplicity (cheap).
- `common_time_rtc` — **NOT needed** (RTC is cascade-only; lifecycle startup/shutdown doesn't touch RTC). **Do NOT add** — fewer deps = smaller R7 blast radius. (If link fails, add; but the moved code provably has no RTC call.)
- `devconf`/`setting`/`env`/`logger`/`daynight`/`gpio`/`power`/`common_time_timezone`/`jsoncpp`/`common_misc` — **required** (startup S1-S13).
- `mcu` — **NOT needed by the moved code** (syncWithMCU stays in main_app; nothing else in startup/shutdown touches MCU). **Do NOT add** unless link fails. (Power::isChangeModeRequested is in `power`, not `mcu`.)

### `src/app/CMakeLists.txt` edits
1. After `:56` (`add_subdirectory(workmode)`), add:
   ```cmake
   add_subdirectory(app_lifecycle)
   ```
2. In the **sim** `htc_main_app` link block (`:104-152`), add `app_lifecycle` (e.g. after `app_workmode` at `:136`).
3. In the **HW** `htc_main_app` link block (`:254-312`), add `app_lifecycle` (e.g. after `app_workmode` at `:290`).
4. **Crucial**: because `app_lifecycle` PUBLIC-links e.g. `media_rtsp`/`http_server`/`storage`/etc., and those are already in `htc_main_app`'s PRIVATE link list, there's no conflict (CMake dedups). No need to remove anything from main_app's list (keep the diff minimal — the moved code is still referenced by main_app's cascade via `lc.config()` etc.? No — once moved, main_app no longer `#include`s MediaScanner/TcpEvent/etc. for the moved functions, but the cascade still uses `RtspServer`, `MdnsService`, `http_server_*` directly, so main_app STILL needs those libs directly. **Do NOT prune main_app's link list in C1** — leave it as-is; over-linking is harmless and keeps the diff to "add app_lifecycle" only.)

---

## 11. Behavior-preservation verification

### Tier 1 — PC-sim, mechanical
1. **Dual-platform build**:
   ```bash
   cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)
   cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)
   ```
   Both must link clean. The T32 link is the R7 gate.
2. **Byte-diff the moved bodies** vs HEAD:
   ```bash
   git show HEAD:src/app/main_app.cpp > /tmp/main_app_HEAD.cpp
   # Extract the signal machinery (:522,:534-651), S1-S16 (:659-998),
   # main_exit (:1494-1548) from HEAD and from ProcessLifecycle.cpp,
   # normalize whitespace, diff. Only allowable delta: `static`→method-wrap,
   # `daynight_switch`→`daynight_switch_`, `gpio_rgb_led`→`gpio_rgb_led_`,
   # `already_in_exit_flow` stays file-scope, `setting_file_path`→`setting_file_path_`.
   # Any logic-line delta = RED FLAG.
   ```
   Implementer produces this diff as evidence.
3. **Sim smoke, all modes** vs baseline:
   ```bash
   # baseline (HEAD binary already in build_sim/bin/htc_main_app — snapshot app.log + files first)
   for wm in 0 1 2 3 4; do for rtc in 0 1; do
       build_sim/bin/htc_main_app -wm $wm -rtc $rtc > /tmp/base_wm${wm}_rtc${rtc}.log 2>&1
   done; done
   build_sim/bin/htc_main_app -m > /tmp/base_m.log 2>&1
   # rebuild with C1, re-run, diff logs + DCIM/media/upload tree
   ```
   Ordered `app.log` + written files + desc JSON must match baseline (modulo timestamps).
4. **SIGTERM sim cleanup-log tail**:
   ```bash
   build_sim/bin/htc_main_app -wm 3 -rtc 1 & PID=$!; sleep 2; kill -TERM $PID; wait $PID
   # diff the "Processing signal 15" + reset log tail vs baseline
   ```

### Tier 2 — PC-sim, semantic review
- **Signal-handler-before-first-blocking-call ordering**: `commonStartup()` (S1-S8) runs `installSignalHandlers()` is called immediately after — handlers installed before dispatch and before any cascade blocking call. Verify no blocking call precedes handler install (today `MediaScanner::startScan` at `:715` is async; `pipe()` at `:780`; first blocking call is the `-wm 3/4` run-loop at `:1315`).
- **Self-pipe non-block flags**: both `g_signal_pipe[0]` and `[1]` get `O_NONBLOCK` — verbatim in `installSignalHandlers()`.
- **`shutdown()` byte-faithful to `:1494-1548`** incl. the `rtsp_singleton_used` "before the freeze" gate (`:1504-1512`): the `if (rtspSingletonUsed_) RtspServer::getInstance()->shutdown();` must be at the same position (step 2, before self-pipe close, before Settings save, before poweroff).
- **`cleanupHook` invoked at the same point** (main thread, only via `waitForSignal()` catching a signal — `:572-575` semantics). NOT invoked on the normal (non-signal) exit path (today `performCleanup` runs only via `waitForSignalOrTimeout`; `main_exit` does NOT call it — verify the hook is NOT called from `shutdown()` directly, only from `waitForSignal()`).
- **`g_active_lc` NOT used** (option A adopted) — grep `ProcessLifecycle.cpp` for any pointer the handler dereferences; expect none.

### Tier 3 — T32 device A/B (MANDATORY, USER-RUN) — **flagged as risk**
Subagents/PM cannot drive the T32 device. The user must run:
- Keep `htc_main_app -wm` baseline (HEAD build in `build/bin/`).
- Per sub-mode (`-wm 0..4 -rtc {0,1}`, `-m`): cold-boot baseline vs C1 build — full `app.log`, produced media + desc JSON, MCU regs after `syncWithMCU`, clean poweroff vs hang.
- **Decisive cheap signal**: does the **next cold boot hang in IMP `configure()`**? (the documented R1 symptom — a missed `RtspServer::getInstance()->shutdown()` in a mode that built the singleton leaves stale IMP driver state). If the C1 build's *next* boot hangs where HEAD's didn't, R1 gate drifted.
- A/B specifically for `-m` (CMD_MOBILE, R2) and `-wm 3` (shares CMD_MOBILE).

### Tier 4 — soak (post-C1, before C5)
N cold boots across all 5 sub-modes — the IMP-configure hang only surfaces across a cold-boot boundary.

---

## 12. Risks (ranked)

- **R1 (highest) — `rtsp_singleton_used` HAL-teardown gate drift**: if `markRtspSingletonUsed()` is missed at `:1296` (CMD_MOBILE) or `:1336` (CMD_RTSP_SERVER), OR if `shutdown()` calls `RtspServer::getInstance()->shutdown()` in a mode that never built the singleton, the next cold boot hangs in IMP `configure()`. **Mitigation**: member set ONLY at the same source lines via `lc.markRtspSingletonUsed()`; `shutdown()` gates on `rtspSingletonUsed_`; Tier 3 device A/B is the decisive check. Sim cannot catch this (sim `Misc::poweroff` is a no-op; `_exit(0)` skips the freeze).
- **R2 — CMD_MOBILE (`:1212-1332`) shared by `-wm 3` and `-m`**: reads `mobile_rtsp_enabled`/`rtsp_audio_enabled`/daynight. These STAY as main_app locals in C1 (move to WorkModeContext is C2). The only rewrites are `daynight_switch`→`lc.daynight()`, `rtsp_singleton_used`→`lc.markRtspSingletonUsed()`, `already_in_exit_flow`→`lc.keepRunning()`, `waitForSignalOrTimeout`→`lc.waitForSignal()`. A/B `-m` and `-wm 3` specifically in Tier 3.
- **R3 — `performCleanup`/`main_exit` double-stop split**: the `:589-632` (mode resets) vs `:1494-1548` (transport teardown) division must be preserved. The split chosen (resets→hook, transport→`shutdown()`) means the hook is NOT called from `shutdown()`, only from `waitForSignal()`. Verify log ordering on the signal path (Tier 2) — today `performCleanup` logs "Processing signal %d" then stops transport; after C1 the hook logs that line + resets, and transport stops at `main_exit` (which runs immediately after the run-loop breaks). Log-equivalent up to ordering of idempotent stop calls.
- **R4 — async-signal-safety of chosen option (A)**: ADOPTED (A) neutralizes this — the handler's instruction stream is unchanged (same `sig_atomic_t` store + `write(2)` over file-scope state, no pointer deref). Residual risk: someone later adds a class member and routes the handler through `g_active_lc` (option B) — the design comment in `ProcessLifecycle.cpp` must forbid this explicitly.
- **R5 — sim masks teardown bugs**: mandatory Tier-3 device A/B (see above). Sim `_exit(0)` skips the poweroff freeze that exposes R1.
- **R6 — one-`ProcessLifecycle`-per-binary invariant**: option A makes signal state file-scope (shared across instances in one binary). Add a debug-only construction-count assert. C1 has exactly one instance; C3's `htc_workmode_app` is a separate process.
- **R7 — `app_lifecycle` link-dep growth on T32**: the lib PUBLIC-links ~12 deps; the T32 cross-link (not just sim) must resolve. Gate: Tier 1 step 1 (HW `cmake --build build`). Prune over-linking (`common_time_rtc`, `mcu` likely unneeded by the moved code — do not add preemptively).
- **R8 — `commonStartupPostDispatch` split ordering**: S9-S13 must run AFTER dispatch (today they're at `:897-998`, after dispatch `:796-895`), because S11 netif selection (`:935`) reads `command` (`CMD_MOBILE`) and `program_type`. The split into `commonStartupPostDispatch()` preserves this. Verify `command` is available to `commonStartupPostDispatch` (pass it via StartupConfig or read from ShutdownContext — cleanest: add `int command` field to `StartupConfig` for the post-dispatch call, or make `commonStartupPostDispatch` take `(const StartupConfig&, int command)`).

---

## 13. Rollback

- C1 is one commit on `feature/new-workmode`. Rollback = `git revert <C1-sha>` (restores `main_app.cpp` monolith + removes `app_lifecycle/`). No data migration, no on-device state. The `htc_main_app -wm` baseline binary in `build/bin/` remains the A/B reference throughout C1-C5.

---

## 14. Acceptance criteria (C1 gate)
1. `src/app/app_lifecycle/{ProcessLifecycle.h,ProcessLifecycle.cpp,CMakeLists.txt}` exist; `app_lifecycle` builds SHARED into `build/lib/libapp_lifecycle.so` and `build_sim/lib/libapp_lifecycle.so`.
2. `htc_main_app` links `app_lifecycle` in both sim and HW; both builds succeed.
3. Byte-diff of moved bodies vs HEAD shows ONLY `static`→method/member rewrites — zero logic-line changes.
4. Sim `-wm 0..4 -rtc {0,1}` + `-m` produce byte-identical (modulo timestamps) `app.log` + media/upload trees vs HEAD baseline.
5. Sim SIGTERM `-wm 3/4` cleanup-log tail matches baseline.
6. `signalHandler` is a free function over file-scope state (option A) — no pointer deref in signal context (grep-verified).
7. `rtsp_singleton_used` set only at `:1296`/`:1336` source lines (via `markRtspSingletonUsed`); `shutdown()` gates `RtspServer::getInstance()->shutdown()` on it.
8. **(user-run, blocking) Tier 3 device A/B**: next cold boot does NOT hang in IMP `configure()` for any sub-mode.
