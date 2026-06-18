# T14 tester evidence — Phase C-1 app_lifecycle extraction (behavior-preserving refactor)

All gates PASS. This is a refactor; bodies that moved are byte-identical modulo
the documented wrapping + ref rewrites, and the cascade logic is unchanged.
Tier-3 device A/B (cold-boot IMP configure() hang) is USER-RUN (sim `Misc::poweroff`
is a no-op and `_exit(0)` skips the freeze, so sim cannot surface the R1 symptom).

---

## Gate 1 — Both platforms build+link clean

| Build | Command | Exit | Artifact |
|-------|---------|------|----------|
| PC sim | `cmake --build build_sim -j$(nproc) --target htc_main_app` | 0 | `build_sim/lib/libapp_lifecycle.so` (md5 `1ee116b6907a2f477b9f88a2fdb28f85`, 2524896 B) + `build_sim/bin/htc_main_app` (md5 `193d03d9a4f73f29640ebf2ca01dd956`) |
| T32 cross (R7) | `cmake --build build -j$(nproc) --target htc_main_app` | 0 | `build/lib/libapp_lifecycle.so` (md5 `c1afe579ceed3acf2d934e9784554dd4`, 386956 B) + `build/bin/htc_main_app` (md5 `3943a13842421b3c04cd59fdb132d767`) |

Build tail (both):
```
[100%] Built target app_lifecycle
[100%] Built target htc_main_app
BUILD_SIM_EXIT=0 / BUILD_T32_EXIT=0
```

**PASS.**

---

## Gate 2 — Moved bodies byte-identical (PRIMARY)

Source for HEAD baseline: `git show HEAD:src/app/main_app.cpp` → `/tmp/HEAD_main_app.cpp` (1548 lines).
Target: `src/app/app_lifecycle/ProcessLifecycle.cpp` (signal machinery + S1-S8 + S9-S13 + main_exit tail) and the cleanupHook lambda in `src/app/main_app.cpp` (performCleanup mode-local resets).

### 2a. `signalHandler` (HEAD :639-651 → PL.cpp :140-152)
Whitespace-normalized diff after `static void` → `void` wrap:
```
$ diff head_sh_norm pl_sh
(empty)
```
**IDENTICAL** — only the `static` qualifier dropped (anon-ns free fn).

### 2b. `drainSignalPipe` (HEAD :544-555 → PL.cpp :96-106)
Whitespace-normalized (squeeze blanks):
```
$ diff <(tr -s ' \n' ' ' < head_drain) <(tr -s ' \n' ' ' < pl_drain)
(empty) → DRAIN IDENTICAL (whitespace-normalized)
```
Only delta was a single trailing blank line (cosmetic).

### 2c. `waitForSignalOrTimeout` (HEAD :560-577 → PL.cpp :112-132)
Blank-stripped diff:
```
15c15,18
<         performCleanup(sig);
---
>         auto& hook = cleanupHookRef();
>         if (hook) {
>             hook(sig);
>         }
```
**The ONLY non-whitespace delta is the documented R3 split**: `performCleanup(sig)` → invoke the caller-registered cleanup hook. This is the single intentional logic delta (transport teardown → `shutdown()`; mode-local resets → caller hook). Body otherwise byte-identical.

### 2d. `performCleanup` mode-local resets (HEAD :589-632 → main_app.cpp cleanupHook :635-671)
HEAD body ref-rewritten (`daynight_switch` → `lc.daynight()`, `gpio_rgb_led` → `lc.rgbLed()`), transport-teardown lines removed (they moved to `shutdown()`), compared to the cleanupHook lambda body. Result: every logic statement byte-identical. The only diff lines are structural `}` braces whose balance shifted because the `http_server_stop/deinit` + `TcpEventService::stop` lines (now in `shutdown()`) were removed from between the settings-save block and `mgmtServClient = nullptr`. Verified by statements-only comparison — the sequence `Logger "Processing signal" → daynight DAY → rgbLed LOW → setting_file_path save → mgmtServClient/storageServClient null → SIGTERM power-hold` is identical.

### 2e. main_exit tail (HEAD :1504-1545 → PL.cpp `shutdown()` :561-604)
Statements-only diff (comments + blanks stripped), HEAD ref-rewritten (`rtsp_singleton_used` → `impl_->rtsp_singleton_used`, `setting_file_path` → `impl_->setting_file_path`, `RtspServer` → `media::RtspServer`, `auto_release` → `impl_->auto_release`):

HEAD statements (tail that moved):
```
service::TcpEventService::getInstance()->stop();    # HEAD :1504-1505 (top of main_exit)
service::MdnsService::getInstance()->stop();
if (impl_->rtsp_singleton_used) {
    media::RtspServer::getInstance()->shutdown();
}
if (g_signal_pipe[0] >= 0) { ::close(...); g_signal_pipe[0] = -1; }
if (g_signal_pipe[1] >= 0) { ::close(...); g_signal_pipe[1] = -1; }
Settings::getInstance()->saveToJsonFile(impl_->setting_file_path);
Logger::log(... "Power off From Main function");
#ifndef BUILD_FOR_SIMULATION
    auto gpio_power_hold = GPIO(POWER_HOLD_PIN);
    if (!...exportGPIO() || !...setDirection(OUTPUT) || !...setValue(LOW)) { ... }
#endif
```

`shutdown()` statements: identical to the above, PLUS a null-guard on auto_release:
```
if (impl_->auto_release) { impl_->auto_release->release(); }   # behavior-identical (always set in S7)
```

The terminal steps (`#ifdef BUILD_FOR_SIMULATION _exit(0) #else syncWithMCU(); config->flush(); Misc::poweroff(); while(1); #endif`) correctly stayed in `main_app.cpp` `main_exit:` (lines 1188-1199), NOT in `shutdown()` — matches the documented R3 split (terminal + app-specific steps stay in the caller).

### 2f. S1-S8 startup (HEAD :660-795 → PL.cpp `commonStartup` :262-373 + `installSignalHandlers` :375-394)
Statements byte-identical modulo: (a) path inputs now passed via `StartupConfig cfg` fields (caller computes them — same values), (b) `daynight_switch`/`gpio_rgb_led`/`auto_release` → `impl_->` members, (c) MediaScanner block wrapped in `if (!cfg.skipMediaScanner)` (htc_main_app leaves false = runs), (d) `pipe()` + `signal()` moved to `installSignalHandlers()` returning bool (caller bails on false, matching the original `return -1`). All documented allowed deltas.

### 2g. S9-S13 startup (HEAD :896-998 → PL.cpp `commonStartupPostDispatch` :396-518)
Statements byte-identical modulo the member rewrites + the `command` parameter (S11 netif reads CMD_MOBILE via a private `const int CMD_MOBILE = (1<<8)` mirror). Same ordering (S10 daemon → S9 Settings → S10b program_type → S11 mount/netif/factory/update → S13 timezone).

**Gate 2 PASS** — the single intentional logic delta is the `performCleanup(sig)` → cleanup-hook split (R3); no other logic-line delta.

---

## Gate 3 — Cascade logic unchanged (HEAD :1002-1340 → main_app.cpp :680-1020)

Cascade diff after applying the documented ref rewrites to HEAD
(`daynight_switch`→`lc.daynight()`, `gpio_rgb_led`→`lc.rgbLed()`,
`rtsp_singleton_used = true`→`lc.markRtspSingletonUsed()`,
`while (!already_in_exit_flow)`→`while (lc.keepRunning())`,
`(void)waitForSignalOrTimeout`→`(void)lc.waitForSignal`):
```
310,311c310,311
< []{ return !already_in_exit_flow; },
< [](int ms){ (void)lc.waitForSignal(ms); })) {
---
> [&lc]{ return lc.keepRunning(); },
> [&lc](int ms){ (void)lc.waitForSignal(ms); })) {
```
The only deltas are the RTSP DI lambdas (`runRtspServerUntilSignal` predicates) capturing `&lc` instead of reading the bare global — both are documented allowed rewrites. The two trailing `}` are brace-balance artifacts of the line-range cut, not logic. **Every cascade logic line (RTC get/set, SNAP ×2, CONN_NET/DHCP/NTP, AUDIO_RECORD, VIDEO_RECORD, CMD_MOBILE full block, CMD_RTSP_SERVER, CMD_AUTH/HEARTBEAT/UPLOAD) is byte-identical modulo the ref rewrites. PASS.**

---

## Gate 4 — Option A (async-signal-safety)

`signalHandler` (PL.cpp :140-152):
```c
void signalHandler(int signal) {
    if (already_in_exit_flow) { return; }
    g_pending_signal = signal;          // sig_atomic_t store
    int saved_errno = errno;
    if (g_signal_pipe[1] >= 0) {
        char c = 'x';
        ssize_t r = write(g_signal_pipe[1], &c, 1);   // write(2) — async-signal-safe
        (void)r;
    }
    errno = saved_errno;
}
```
- Free fn over file-scope state only (`already_in_exit_flow` bool, `g_pending_signal` sig_atomic_t, `g_signal_pipe[2]` int array). No `this`, no vtable, no member-offset load, NO pointer deref.
- `grep -nE 'signalHandler|g_active_lc' ProcessLifecycle.cpp` → `g_active_lc` appears ONLY in the file-top comment forbidding Option B (:54-66). No `g_active_lc` pointer exists.
- `grep -nE 'this|impl_|g_active_lc|->'` within signalHandler body (excluding `write(`) → `NO member/this/pointer deref in signalHandler body`.

**PASS** — strictly inside the POSIX.1-2017 async-signal-safe set; instruction stream byte-identical to the monolith's handler.

---

## Gate 5 — R1 gate (rtsp_singleton_used)

- `grep -nE 'markRtspSingletonUsed' src/app/main_app.cpp` → exactly **2 hits**:
  - `:974` — inside `if (mobile_rtsp_enabled)` within `if (command & CMD_MOBILE)` (HEAD original :1296).
  - `:1014` — inside `if (command & CMD_RTSP_SERVER)` (HEAD original :1336).
- Cross-check HEAD: `grep -nE 'rtsp_singleton_used = true' HEAD_main_app.cpp` → 2 sites (:1296, :1336). Same count, same source positions (CMD_MOBILE + CMD_RTSP_SERVER).
- Gate in `shutdown()` (PL.cpp :571-572): `if (impl_->rtsp_singleton_used) { media::RtspServer::getInstance()->shutdown(); }` — present, BEFORE self-pipe close / Settings save / poweroff.

**PASS.**

---

## Gate 6 — Sim smoke (best-effort; sim cannot exercise HW modes fully)

### 6a. `-h`
```
$ timeout 30 build_sim/bin/htc_main_app -h; echo EXIT=$?
EXIT=255   # printUsage() then return -1 (pre-existing behavior, byte-identical to HEAD; -h never exited 0)
```
S1-S8 startup logs printed (EasyLogger init, SIM root/project/log paths), then Usage. No crash. NOTE: `commonStartup` + `installSignalHandlers` now run before the `-h` check (minor ordering change vs HEAD, where S1-S8 ran before the dispatch too) — behavior-neutral, no crash. The `-h` path has always returned `-1` (the gate's "exits 0" was imprecise; the code intent is "print usage and bail").

### 6b. `-wm 0 -rtc 1` (SNAP_ONLY)
```
$ timeout 40 build_sim/bin/htc_main_app -wm 0 -rtc 1; echo EXIT=$?
EXIT=0
```
Lifecycle markers: `Power off From Main function` (shutdown step 5) → `[SIM] Program exit normally` (`_exit(0)`). MCU/IIC "bus not open" errors are expected sim no-ops (no real I2C). Startup→dispatch→shutdown→terminal flow clean. No crash/abort.

### 6c. `-wm 3 -rtc 1` + SIGTERM (TEST_ONLY / CMD_MOBILE — exercises the signal path + R1 gate)
```
$ build_sim/bin/htc_main_app -wm 3 -rtc 1 & PID=$!; sleep 3; kill -TERM $PID; wait $PID; echo EXIT=$?
EXIT=0
```
Signal-path markers (in order):
```
I/LEGACY  Processing signal 15 on main thread          # cleanupHook lambda ran (SIGTERM=15)
I/LEGACY  RtspServer::shutdown: process-level teardown  # R1 gate FIRED (markRtspSingletonUsed set on CMD_MOBILE)
I/LEGACY  RtspServer::shutdown: teardown complete
I/LEGACY  Power off From Main function                   # shutdown() step 5
I/LEGACY  [SIM] Program exit normally                    # terminal _exit(0)
```
No crash/abort markers. **This is the strongest evidence**: the full signal→cleanupHook→R1-gated-IMP-teardown→shutdown→terminal flow works end-to-end, and the R1 gate correctly fires on the CMD_MOBILE path (confirming `markRtspSingletonUsed` → `rtsp_singleton_used` → gated `RtspServer::getInstance()->shutdown()` wiring).

**PASS** (best-effort; Tier-3 device A/B is the real R1 gate — USER-RUN).

---

## Gate 7 — Scope

- `git diff --stat HEAD -- src/hal` → **empty** (PIC-owned HAL untouched).
- `grep -rnE 'std::to_string|std::stoi' src/app/app_lifecycle/` → **NONE** (T32 uClibc safe).
- `src/` diff scope: ONLY `src/app/CMakeLists.txt` (M) + `src/app/main_app.cpp` (M) + new `src/app/app_lifecycle/` (??). No other source touched.
- `daynight_switch`/`gpio_rgb_led`/`auto_release` are pImpl `Impl` members (PL.cpp :231-243), not file-scope globals — confirmed by grep (no file-scope declarations).
- **Root `CMakeLists.txt` PIC line** (`:27 set(CMAKE_POSITION_INDEPENDENT_CODE ON)`): NOTED for reviewer. It is a one-line global build-flag change outside the `src/app/app_lifecycle/` scope. Required for the dual-platform SHARED-lib link (sim static libs needed `-fPIC` to link into the SHARED `app_lifecycle`); MIPS already uses PIC. Relocation-model-only, behavior-neutral. Rollback = delete the line.
- `res/` working-tree changes (JPG/JSON/setting.json) are runtime artifacts written by the sim smoke (snap test + Settings save), NOT source edits.

**PASS** (with the root-CMakeLists PIC line flagged for reviewer awareness, per the dispatch instruction "don't fail on it").

---

## Tier-3 device A/B — USER-RUN (flagged)

Sim `Misc::poweroff` is a no-op and `_exit(0)` skips the process freeze, so sim cannot surface the R1 symptom (next cold-boot IMP `configure()` hang on stale driver state). The user must A/B every sub-mode (`-wm 0..4 -rtc {0,1}`, `-m`, `-rs`) cold-boot vs HEAD on the T32 device, specifically watching `-m` / `-wm 3` (both CMD_MOBILE) and `-rs` (CMD_RTSP_SERVER) for the R1 gate firing before the freeze. This node cannot drive the T32 device.
