# T9 Reviewer Evidence — Phase B-1 manifest extraction

Reviewer: independent final gate. Branch `feature/new-workmode`, worktree `new-workmode`.
Baseline: `git show HEAD:src/app/main_app.cpp` (HEAD = 8df7fe8).

## Review table

| # | Focus | Verdict | Evidence |
|---|-------|---------|----------|
| 1 | Byte-identical bodies (3 fns) | PASS | Body-diff vs HEAD: only `static int`→`int` signature + namespace wrapper changed; logic 100% identical. See §A. |
| 2 | CMake deviation #1 — manifest include path | PASS | `${CMAKE_CURRENT_SOURCE_DIR}/..` added in `src/manifest/CMakeLists.txt:9`; app adds `${...}/../manifest` in `src/app/CMakeLists.txt`. Standard tree pattern (`../service`, `../storage`). No shadow — `manifest/Manifest.h` is unique in tree. main_app.cpp:65 `#include "manifest/Manifest.h"`. |
| 3 | CMake deviation #2 — `crc16` in manifest link list | PASS (real latent bug, correct workaround) | Missing dep is REAL — see §B. Fix is additive + correct; proper home is `common_utils_crc`'s CMakeLists. Noted as follow-up, not a blocker. |
| 4 | Symbol / ODR safety | PASS | `getFileCreationTime` is file-local `static` in Manifest.cpp:27, fully removed from main_app.cpp (grep: zero hits outside manifest). `generateDescInfo`/`createDescInfoFile` mangled into `manifest::` namespace — unique names. See §C. |
| 5 | Both builds link libmanifest.so as NEEDED | PASS | `readelf -d` shows `libmanifest.so` NEEDED in both `build/bin/htc_main_app` and `build_sim/bin/htc_main_app`. App references `_ZN8manifest16generateDescInfo...` / `_ZN8manifest18createDescInfoFile...` as UND. See §D. |
| 6 | Scope discipline | PASS (code) / NOTE (worktree) | Code delta = exactly `src/manifest/*` (new) + `src/CMakeLists.txt` + `src/app/CMakeLists.txt` + `src/app/main_app.cpp`. Nothing under `src/hal/**`. Singletons still read via `::getInstance()` (no decoupling — matches plan). NOTE: working tree also carries unrelated `res/config.sim.ini`, `doc/knowledge/working-set.md`, `orchestration-state.yaml` — not T9's code delta; see §E. |
| 7 | T32 uClibc-hostile std | PASS | Manifest.cpp: zero `std::to_string`/`stoi`/`stol`/`stod`/`stof`/`stoul`/`to_wstring`. Uses `to_string_custom` (StringConvert.h) at lines 180-182. |

## §A — Body-diff (independent re-run)

Method: extracted each function from `git show HEAD:src/app/main_app.cpp` and diffed against `src/manifest/Manifest.cpp`, dropping only the signature line.

- `getFileCreationTime` (HEAD L83 vs Manifest L27): **IDENTICAL** (single trailing-blank artifact `40d39` is the separator before the next function in HEAD, not body content).
- `generateDescInfo` (HEAD L268 vs Manifest L68): **IDENTICAL** (`184d183` = same trailing separator artifact).
- `createDescInfoFile` (HEAD L453 vs Manifest L253): **IDENTICAL** (only signature `static int`→`int`).

Zero logic drift. `getFileCreationTime` kept `static` (correct — stays file-local in the new TU).

## §B — crc16 dependency analysis (the load-bearing judgment)

**Claim:** `common_utils_crc` has a latent missing dep on `crc16` (symbol `cal_crc16`), surfaced when `manifest` transitively pulls `common_utils_crc` without `crc16`.

**Verdict: CLAIM IS CORRECT. The missing dependency is real.**

Evidence:
- `src/common/utils/crc/CRC.cpp:31` calls `cal_crc16(crc, buffer.data(), n)` inside `CRC::calculate_crc16`.
- `cal_crc16` is defined ONLY in `third_party/crc16/src/crc16.cpp:43` (header `third_party/crc16/include/crc16.h:8`), exposed via the `crc16` SHARED lib target (`third_party/crc16/CMakeLists.txt:12`).
- `src/common/utils/crc/CMakeLists.txt:11` declares only `target_link_libraries(common_utils_crc PRIVATE logger)` — **never links `crc16`** despite depending on its symbol. This is the latent bug.

**Why it was masked before:** `htc_main_app` (`src/app/CMakeLists.txt`) links `common_utils_crc` AND `crc16` directly on its own link line (lines 132, 286), so the undefined `cal_crc16` reference in `libcommon_utils_crc.so` was resolved by `crc16` appearing on the final executable link. Shared-library symbol resolution is lazy at executable link time, so the gap was invisible as long as every consumer also linked `crc16`.

**Why it surfaced for manifest:** `manifest` is a SHARED lib that links `common_utils_crc`. When `manifest.so` is produced it would carry an unresolved `cal_crc16` unless `crc16` is also on manifest's link line. Adding `crc16` to manifest's PUBLIC link list (`src/manifest/CMakeLists.txt:37`) resolves it.

**Is the fix in the right place?** The architecturally-correct fix is to declare the dependency at its source: change `src/common/utils/crc/CMakeLists.txt` to `target_link_libraries(common_utils_crc PRIVATE logger crc16)` (or PUBLIC if the header exposes crc16 types — it does include `crc16.h`, so PUBLIC is defensible). Then every consumer inherits it transitively and no consumer needs to know about `crc16`. The implementer's workaround (add `crc16` to manifest) is **additive, correct, and the build is sound** — both `build/lib/libmanifest.so` and `build_sim/lib/libmanifest.so` carry `libcrc16.so` as NEEDED (confirmed via `readelf -d`). Not a blocker for T9; recorded as a follow-up cleanup.

## §C — Symbol / ODR safety

- `getFileCreationTime`: `grep -rn getFileCreationTime src` → only `src/manifest/Manifest.cpp` (def L27 + call L93). Fully removed from main_app.cpp. `static` ⇒ internal linkage, no collision risk even if another TU defines the same name.
- `generateDescInfo` / `createDescInfoFile`: mangled into `manifest::` namespace. Confirmed exported symbols:
  - T32 `build/lib/libmanifest.so`: `_ZN8manifest16generateDescInfo...` (GLOBAL FUNC, 14116 bytes), `_ZN8manifest18createDescInfoFile...` (GLOBAL FUNC, 264 bytes).
  - SIM `build_sim/lib/libmanifest.so`: same two symbols, `T` (global text).
- App references them as `U` (UND) — resolved at load by libmanifest.so. No global `::generateDescInfo` exists, so no ODR collision.

## §D — Build / link evidence

```
# T32
readelf -d build/bin/htc_main_app | grep manifest
  0x00000001 (NEEDED) Shared library: [libmanifest.so]

readelf -d build/lib/libmanifest.so | grep -iE 'NEEDED.*(crc|misc|json)'
  NEEDED [libcommon_misc.so] [libcommon_utils_crc.so] [libcrc16.so] [libjsoncpp.so]

# SIM
readelf -d build_sim/bin/htc_main_app | grep manifest
  0x0000000000000001 (NEEDED) Shared library: [libmanifest.so]

readelf -d build_sim/lib/libmanifest.so | grep NEEDED
  NEEDED [libcommon_misc.so] [libcommon_utils_crc.so] [libcrc16.so]
         [libcommon_time_timezone.so] [libsetting.so] [libdevconf.so]
         [libmcu.so] [libdisk.so] [liblogger.so] [libjsoncpp.so]

# App symbol references (SIM)
nm -D build_sim/bin/htc_main_app | grep manifest
  U _ZN8manifest16generateDescInfo...
  U _ZN8manifest18createDescInfoFile...
```

`libmanifest.so` is a genuine NEEDED dependency of the app on both platforms (not orphaned). `libcrc16.so` is NEEDED of `libmanifest.so` on both platforms (crc16 workaround effective at binary level).

## §E — Scope note (working-tree extras, NOT T9 code delta)

`git diff --stat HEAD` also shows:
- `res/config.sim.ini` (82 lines) — full content swap from a `[POLICY]/[NTP]/[SERVER]` sample to a `[BOOT]/[DEVICE]/[SYSTEM]` config. This is sim test-config regeneration (tester/sim-run scaffolding), not introduced by the manifest extraction.
- `doc/knowledge/working-set.md` (+7) — adds a workmode-sdk-extraction note referencing T8 design docs (broader feature arc bookkeeping).
- `orchestration-state.yaml` (+14) — orchestration bookkeeping.

None of these alter the T9 code change. The T9 substantive code delta is exactly: new `src/manifest/{CMakeLists.txt,Manifest.h,Manifest.cpp}` + the 3 `src/` edits. No `src/hal/**` touched. No singleton decoupling (Manifest.cpp still reads `Settings::getInstance()`, `MCU::getInstance()`, `DeviceConfig::getInstance()`).

## Commands (reproducible)

```
git show HEAD:src/app/main_app.cpp                           # body-diff baseline
readelf -d build/lib/htc_main_app | grep -i manifest         # T32 NEEDED
readelf -d build_sim/bin/htc_main_app | grep -i manifest     # SIM NEEDED
readelf -d build/lib/libmanifest.so | grep -iE 'NEEDED.*crc' # crc16 NEEDED
grep -nE 'cal_crc16' src/common/utils/crc/CRC.cpp            # crc16 symbol use
grep -n crc16 src/common/utils/crc/CMakeLists.txt            # (absent → missing dep)
nm -D build_sim/lib/libmanifest.so | grep -iE 'generateDescInfo|createDescInfoFile'
grep -nE 'std::to_string|std::stoi' src/manifest/Manifest.cpp # zero
```
