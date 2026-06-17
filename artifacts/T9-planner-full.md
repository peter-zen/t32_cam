# T9 — Phase B-1 Plan: extract `manifest` library (behavior-preserving)

Task T9, node=planner, flow=feature. Branch `feature/new-workmode`.
Scope: **verbatim-move-first** extraction of `generateDescInfo` and
`createDescInfoFile` out of `src/app/main_app.cpp` into a new `manifest`
library, linked into `htc_main_app` on **both** sim and hw. Behavior must be
**byte-identical**. Decoupling singletons is explicitly OUT OF SCOPE (follow-up).

All facts below were verified by reading the repo (grep/read evidence in the
report card `verification.commands`).

---

## 0. Verified ground truth (evidence-backed)

- `generateDescInfo` lives at `src/app/main_app.cpp:268-451`.
- `createDescInfoFile` lives at `src/app/main_app.cpp:453-471`; internal call to
  `generateDescInfo` at `:456`.
- `getFileCreationTime` is a `static` at `src/app/main_app.cpp:83-122`, used
  **only** by `generateDescInfo` (grep shows no other definition anywhere in
  `src/`). -> **Must move into the lib** (private/internal; not part of public API).
- 4 external call sites in main_app.cpp:
  - `:609`  `createDescInfoFile(file_names, desc_filename);`  (processCmdSnap)
  - `:621`  `createDescInfoFile(file_names, "./res/20250620_101358.json");`
  - `:716`  `generateDescInfo(files, desc_info)` direct call (processCmdVideoRecord)
  - `:790`  `createDescInfoFile(all_files, desc_filename);` (processCmdConcurrentSnapRecord)
- Constants/macros all in `src/common/Common.h`:
  `YEAR_OFFSET`(:209), `MONTH_OFFSET`(:210), `DISK_PATHNAME`(:83),
  `EC_SUCCESS`(:92), `EC_OPEN_FILE_FAILED`(:97), `PTYPE_USB_DONGLE`(:79).
- `USER_CONFIG_WPWS` is **never `add_definition`'d** anywhere (top-level
  `CMakeLists.txt` defines `POWER_MANAGER_ON`/`RTC_EXIST`/`MCU_EXIST`/etc. but
  NOT `USER_CONFIG_WPWS`). So the `#if USER_CONFIG_WPWS` branch is
  dead-but-still-compiled; the `#else` (temp/humidity sensor) branch is the live
  path. Both branches only touch `MCU`, so `MCU.h` covers both. The macro needs
  no special handling — it's just an undefined preprocessor symbol that
  evaluates to 0.
- `to_string_custom` is a header-only template in
  `src/common/utils/string/StringConvert.h` (overloads at :87/:96/:110). No link dep.
- Singletons (kept coupled — out of scope to decouple):
  `Settings::getInstance()` (`src/config/setting/Settings.h`),
  `MCU::getInstance()` (`src/hardware/mcu/MCU.h`),
  `DeviceConfig::getInstance()` (`src/config/devconf/DeviceConfig.h`).
- Free helpers used:
  `Timezone::getFormattedTimeWithTimezone` (`src/common/time/timezone/Timezone.h`),
  `Misc::getFilepath/getFilename/getIPAddress/getNetworkInterfaceName`
  (`src/common/misc/Misc.h`),
  `CRC::calculate_crc16` (`src/common/utils/crc/CRC.h`),
  `Disk::getInfo` (`src/hardware/disk/Disk.h`),
  `Logger::log` (`src/logger/Logger.h` — via `logger` lib).
- Third-party: `jsoncpp` (`<json/json.h>`).
- POSIX: `gettimeofday`/`struct timeval` (`<sys/time.h>`), `stat` (`<sys/stat.h>`).
- Library convention in this repo: every leaf lib is `add_library(<name> SHARED ...)`
  with `LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib`. See `disk`, `mcu`,
  `app_workmode` (the structural sibling — it's the closest analog: an
  app-adjacent SHARED lib linked into `htc_main_app`).

---

## 1. Library location + name + linkage

- **Location:** new directory `src/manifest/`.
- **Target name:** `manifest`.
- **Linkage: SHARED** (matches repo convention — `disk`, `mcu`, `app_workmode`
  are all SHARED; app loads `.so` from `build/lib/`). A STATIC lib would be
  fine link-wise but breaks the established "every module is a .so" pattern and
  would be the odd one out; SHARED keeps the `.so` set uniform and the
  deployment/`flatten_lib_symlinks.sh` flow unchanged.

### `src/manifest/CMakeLists.txt`

```cmake
# manifest library: desc-info JSON generation (extracted verbatim from main_app.cpp).
# Behavior-preserving move; singletons still read internally (decoupling = follow-up).

file(GLOB MANIFEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    # common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/misc
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/string
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/utils/crc
    ${CMAKE_CURRENT_SOURCE_DIR}/../common/time/timezone
    # logger
    ${CMAKE_CURRENT_SOURCE_DIR}/../logger
    # config singletons (read-only, kept coupled)
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/setting
    ${CMAKE_CURRENT_SOURCE_DIR}/../config/devconf
    # hardware singletons/helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/mcu
    ${CMAKE_CURRENT_SOURCE_DIR}/../hardware/disk
    # jsoncpp
    ${THIRD_PARTY_PATH}/jsoncpp/include
)

add_library(manifest SHARED ${MANIFEST_SOURCES})

# PUBLIC because the header exposes Settings/MCU/DeviceConfig/json types indirectly;
# transitive deps make the consumer (htc_main_app) link cleanly without re-listing them.
target_link_libraries(manifest
    PUBLIC
        jsoncpp
        common_misc          # Misc::*
        common_utils_crc     # CRC::calculate_crc16
        common_time_timezone # Timezone::getFormattedTimeWithTimezone
        setting              # Settings::getInstance()
        devconf              # DeviceConfig::getInstance()
        mcu                  # MCU::getInstance()
        disk                 # Disk::getInfo
        logger               # Logger::log
)

set_target_properties(manifest PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
```

Notes:
- `to_string_custom` is header-only — no lib, just include dir.
- `common_misc`/`common_utils_crc`/`common_time_timezone` are the actual target
  names already linked into `htc_main_app` (see `src/app/CMakeLists.txt`).
- `crc16` (lowercase) is a *different* lib (the `crc16` target) from
  `common_utils_crc` (which wraps `CRC::calculate_crc16`); the body calls
  `CRC::calculate_crc16` -> needs `common_utils_crc`, **not** `crc16`.

---

## 2. Header — `src/manifest/Manifest.h`

Signatures kept **identical** to today (same param types, same returns) so call
sites change by only a `manifest::` prefix. `getFileCreationTime` is NOT exposed
(it stays a file-local `static` inside `Manifest.cpp`).

```cpp
#ifndef MANIFEST_MANIFEST_H
#define MANIFEST_MANIFEST_H

#include <string>
#include <vector>

namespace manifest {

// Build the 5-section desc JSON (F_UploadedTag, file_inf[], device{}, data{},
// network{}, signal{}) for the given media files. Output written to `desc_info`.
// Returns EC_SUCCESS (0) on success, -1 on time-format failure.
// (Signature unchanged from main_app.cpp:268.)
int generateDescInfo(std::vector<std::string>& files, std::string& desc_info);

// Generate desc info and write it to `desc_filename`. Returns EC_SUCCESS on
// success, EC_OPEN_FILE_FAILED (-5) if the file cannot be opened, or the
// generateDescInfo return on failure.
// (Signature unchanged from main_app.cpp:453.)
int createDescInfoFile(std::vector<std::string>& media_files,
                       const std::string& desc_filename);

} // namespace manifest

#endif // MANIFEST_MANIFEST_H
```

---

## 3. Source — `src/manifest/Manifest.cpp`

The body is **byte-for-byte identical** to the current `main_app.cpp` defs of
`getFileCreationTime` (83-122), `generateDescInfo` (268-451), and
`createDescInfoFile` (453-471). Only changes: (a) the two public functions get
`manifest::` prefix; (b) `getFileCreationTime` stays `static` (file-local);
(c) the `#include` block.

Required `#include`s (mirror what main_app.cpp currently relies on for these
three functions; do NOT copy unrelated includes):

```cpp
#include "manifest/Manifest.h"

#include <string>
#include <vector>
#include <cstring>     // memset/strncpy
#include <cstdlib>     // atoi
#include <sys/stat.h>  // stat
#include <sys/time.h>  // gettimeofday, struct timeval
#include <cstdio>      // fopen/fwrite/fclose, snprintf

#include <json/json.h>

#include "Common.h"                       // YEAR_OFFSET, MONTH_OFFSET, DISK_PATHNAME,
                                          // EC_SUCCESS, EC_OPEN_FILE_FAILED, PTYPE_USB_DONGLE
#include "Logger.h"
#include "StringConvert.h"                // to_string_custom (header-only template)
#include "misc/Misc.h"                    // Misc::getFilepath/getFilename/getIPAddress/getNetworkInterfaceName
#include "utils/crc/CRC.h"                // CRC::calculate_crc16
#include "Timezone.h"                     // Timezone::getFormattedTimeWithTimezone
#include "Settings.h"                     // Settings::getInstance()
#include "MCU.h"                          // MCU::getInstance()
#include "DeviceConfig.h"                 // DeviceConfig::getInstance()
#include "Disk.h"                         // Disk::getInfo
```

Note on header include style: main_app.cpp uses bare `"Misc.h"`, `"CRC.h"`,
`"Timezone.h"`, `"Settings.h"`, `"MCU.h"`, `"Disk.h"`, `"DeviceConfig.h"`,
`"StringConvert.h"`, `"Common.h"`, `"Logger.h"` because `src/app/CMakeLists.txt`
adds each of those dirs to the include path. The manifest CMakeLists above
mirrors that (each subdir is in `include_directories`), so the **same bare
`#include` forms work unchanged** — keeping the body byte-identical. Implementer
may use either bare or path-qualified forms as long as both platforms compile;
bare forms minimize diff.

---

## 4. main_app.cpp edits

1. **Delete** three `static` defs:
   - `getFileCreationTime` (lines 83-122)
   - `generateDescInfo` (lines 268-451)
   - `createDescInfoFile` (lines 453-471)

2. **Add** include near the other local includes (after line 64 region):
   ```cpp
   #include "manifest/Manifest.h"
   ```
   (`src/app/CMakeLists.txt` already adds `${CMAKE_CURRENT_SOURCE_DIR}` to the
   include path, but `manifest/` is under `src/manifest/`, so the app CMake
   needs the new include dir too — see §5.)

3. **Update the 4 external call sites** (add `manifest::` prefix):

   | Line | Before | After |
   |------|--------|-------|
   | 609  | `createDescInfoFile(file_names, desc_filename);` | `manifest::createDescInfoFile(file_names, desc_filename);` |
   | 621  | `createDescInfoFile(file_names, "./res/20250620_101358.json");` | `manifest::createDescInfoFile(file_names, "./res/20250620_101358.json");` |
   | 716  | `if (generateDescInfo(files, desc_info) == 0)` | `if (manifest::generateDescInfo(files, desc_info) == 0)` |
   | 790  | `createDescInfoFile(all_files, desc_filename);` | `manifest::createDescInfoFile(all_files, desc_filename);` |

   (Internal call at `:456` moves with the body — no call-site edit needed.)

4. **`getFileCreationTime` handling:** it was a `static` used **only** by
   `generateDescInfo` -> moved into `Manifest.cpp` as a file-local `static`. No
   other code references it (grep-confirmed), so nothing else breaks.

5. **Do NOT** remove any now-unused `#include`s from main_app.cpp that were
   shared with other code in the file (e.g. `<json/json.h>`, `Settings.h`,
   `MCU.h`, `Common.h`, etc. are all still used elsewhere in main_app.cpp).
   Leaving them is safe and keeps the diff minimal/behavior-preserving.

---

## 5. CMake wiring

### 5a. `src/CMakeLists.txt` — add the subdirectory

Insert `add_subdirectory(manifest)` alongside the other module subdirs
(e.g. after `add_subdirectory(hardware)` / before `add_subdirectory(app)`):

```cmake
add_subdirectory(hardware)
add_subdirectory(platform)
add_subdirectory(service)
add_subdirectory(storage)
add_subdirectory(manifest)   # <-- NEW (must come before app, since app links it)
add_subdirectory(app)
```

### 5b. `src/app/CMakeLists.txt` — add manifest include dir + link on BOTH platforms

Add to the top `include_directories(...)` block (it already has
`${CMAKE_CURRENT_SOURCE_DIR}/../common`, etc.):
```cmake
${CMAKE_CURRENT_SOURCE_DIR}/../manifest
```

Then add `manifest` to the `htc_main_app` link list in **both** the sim block
(`if(BUILD_FOR_SIMULATION)`, ~line 103) and the hw block (`else()`, ~line 252):

**Sim block** (`htc_main_app` PRIVATE list, after `app_workmode`/`logger`, ~line 135):
```cmake
        app_workmode
        manifest          # <-- NEW
        logger
```

**HW block** (`htc_main_app` PRIVATE list, after `app_workmode`/`logger`, ~line 288):
```cmake
        app_workmode
        manifest          # <-- NEW
        logger
```

Because `manifest` is declared `PUBLIC` on its deps (`jsoncpp`, `common_misc`,
`common_utils_crc`, `common_time_timezone`, `setting`, `devconf`, `mcu`, `disk`,
`logger`), and `htc_main_app` already links all of those, there is **no risk of
duplicate-symbol or missing-symbol** — CMake de-duplicates. No changes needed to
`htc_media_app`/`htc_daemon_app`/`htc_wifi_app`/`snap_test` (they don't call
these functions).

### Exact link-dep enumeration for the `manifest` target

| Symbol source | Provided by target | Header |
|---------------|--------------------|--------|
| `Settings::getInstance()` | `setting` | `Settings.h` |
| `MCU::getInstance()` + `readGps`/`convertVoltage`/`readSignal*`/etc. | `mcu` | `MCU.h` |
| `DeviceConfig::getInstance()` | `devconf` | `DeviceConfig.h` |
| `Misc::getFilepath`/`getFilename`/`getIPAddress`/`getNetworkInterfaceName` | `common_misc` | `misc/Misc.h` |
| `CRC::calculate_crc16` | `common_utils_crc` | `utils/crc/CRC.h` |
| `Disk::getInfo` | `disk` | `Disk.h` |
| `Timezone::getFormattedTimeWithTimezone` | `common_time_timezone` | `Timezone.h` |
| `Logger::log` | `logger` | `Logger.h` |
| `Json::*`, `Json::writeString` | `jsoncpp` | `<json/json.h>` |
| `to_string_custom` | (none — header-only template) | `StringConvert.h` |
| `gettimeofday`, `stat`, `atoi`, `memset`, `strncpy`, `fopen`/`fwrite`/`fclose`, `snprintf` | libc/libstdc++ (implicit) | `<sys/time.h>`/`<sys/stat.h>`/`<cstring>`/`<cstdlib>`/`<cstdio>` |

---

## 6. Behavior-preservation verification strategy (for tester/reviewer)

A verbatim move's proof has three tiers; the lightest that still catches a
regression is **(a)+(b)+sim golden (c-light)**:

**(a) Both platforms build + link clean.**
```bash
cmake -DBUILD_FOR_SIMULATION=ON -B build_sim -S . && cmake --build build_sim -j$(nproc)
cmake -DCMAKE_TOOLCHAIN_FILE=toolchain.cmake -B build -S . && cmake --build build -j$(nproc)
```
Must produce `build_sim/lib/libmanifest.so` and `build/lib/libmanifest.so` and a
freshly linked `htc_main_app` referencing it.

**(b) Reviewer diff: moved body is byte-identical to the original.**
Reviewer extracts the original `generateDescInfo`/`createDescInfoFile`/
`getFileCreationTime` text (from git: `git show HEAD:src/app/main_app.cpp`)
and diffs against `src/manifest/Manifest.cpp` ignoring only the `manifest::`
prefix lines and the include block. Zero substantive diff = pass.

**(c) Sim golden test (recommended — the gold standard, low cost here).**
On sim, `MCU`/`Settings`/`DeviceConfig` are stubs returning deterministic values,
so `generateDescInfo` produces **deterministic JSON**. Cheapest viable proof:
- **Pre-move baseline (capture once):** build sim at HEAD, run a sim snap that
  emits a desc JSON, save it as `artifacts/T9-golden-desc.json`. (The body has
  a built-in test path: when `file_names` is empty it snaps `./res/...JPG` and
  writes `./res/20250620_101358.json` via `createDescInfoFile` — see `:612-622`.
  Or call `processCmdSnap`.) 
- **Post-move:** rebuild sim, re-run the same path, capture the desc JSON.
- **Compare:** `diff` must be empty (modulo the volatile `UTime` field, which
  comes from `Timezone::getFormattedTimeWithTimezone(tv.tv_sec)` — on sim this
  is deterministic given a fixed input time; if not, golden-compare must exclude
  `UTime`). 

Recommended lightest-yet-real proof: **(a) + (b) + a sim smoke that just
confirms a desc JSON file is produced and parses as valid JSON** (`python -c
"import json; json.load(open(f))"`). Full golden-compare is a stretch goal; the
byte-identical diff (b) already carries most of the guarantee for a verbatim
move. If the tester can capture a pre-move baseline cheaply, do the golden diff;
otherwise (a)+(b)+JSON-validity is acceptable for T9 and note golden as a T9
follow-up hardening.

Hardware correctness is implied by (b) byte-identical body + (a) clean T32 link;
no on-device behavior change is possible from a verbatim move into a new .so
that re-reads the same singletons.

---

## 7. Risks

| # | Risk | Likelihood | Impact | Mitigation |
|---|------|-----------|--------|------------|
| R1 | **T32 uClibc lacks `std::to_string`/`stoi`** (known project gotcha — link fails on T32, passes sim). | Low | High (T32 link break) | Body already uses `to_string_custom` exclusively; grep `Manifest.cpp` for `std::to_string`/`std::stoi`/`std::stoul` before considering it done — must be **zero hits**. The body uses `snprintf`/`atoi` otherwise. Reviewer checks this. |
| R2 | `USER_CONFIG_WPWS` branch. | Low | Low | Never defined anywhere -> `#else` always compiles; `#if` branch is dead code that still must compile (it only calls `mcu->readEvent*`, covered by `MCU.h`). No action beyond ensuring `MCU.h` is included. |
| R3 | Symbol collision / ODR: if `getFileCreationTime` were also defined elsewhere it'd clash when both TU's link. | Low | Med | Grep-confirmed it's **only** in main_app.cpp. Keeping it `static` (file-local) in `Manifest.cpp` guarantees no external-linkage collision. |
| R4 | Missing include dir -> app can't find `manifest/Manifest.h`. | Med | Low (build error, obvious) | Add `${...}/../manifest` to app's `include_directories` (§5b). |
| R5 | Missing `manifest` in the hw link list (only added to sim). | Med | Med (T32 link fails) | Add to **both** blocks — explicitly called out in §5b with line refs. |
| R6 | `manifest` `PUBLIC`-links a lib the app doesn't, causing an unexpected transitive pull-in. | Low | Low | All `manifest` deps are already in `htc_main_app`'s link list — no new transitive deps. |
| R7 | Behavior change from accidental edit during the move. | Med | High | Gate = byte-identical reviewer diff (§6b). Verbatim copy-paste, prefix-only edits. |
| R8 | `CRC::calculate_crc16` vs the `crc16` lib confusion. | Low | Low (link error, obvious) | Link `common_utils_crc` (the one with the `CRC::` symbol), NOT `crc16`. Spelled out in §1/§5. |
| R9 | Golden test's `UTime` field is time-dependent -> false diff. | Med | Low | Either fix input time or exclude `UTime` from the golden compare. Document in the tester's golden harness. |

**Rollback:** T9 is a pure additive extraction. Rollback = `git revert` the T9
commit (restores the 3 statics in main_app.cpp, removes `src/manifest/`). No
data / config / state migration involved, so rollback is trivial and atomic.
The natural rollback checkpoint is the T9 commit itself; if the implementer
lands it in multiple commits (lib add, app rewire), each is independently
revertible as long as the app rewire commit is the one that removes the statics.

---

## 8. Acceptance criteria (T9 gate)

1. `src/manifest/Manifest.h` + `src/manifest/Manifest.cpp` +
   `src/manifest/CMakeLists.txt` exist; `Manifest.cpp` bodies are byte-identical
   to the pre-move `main_app.cpp` definitions (reviewer-diff-verified).
2. `src/CMakeLists.txt` has `add_subdirectory(manifest)` before `app`.
3. `src/app/CMakeLists.txt` adds the manifest include dir and links `manifest`
   into `htc_main_app` in **both** sim and hw blocks.
4. `src/app/main_app.cpp`: the 3 statics removed; `#include "manifest/Manifest.h"`
   added; the 4 external call sites carry `manifest::` prefix.
5. Both platforms build+link clean; `libmanifest.so` appears in
   `build_sim/lib/` and `build/lib/`.
6. Sim produces a desc JSON for a snap/record path and it is valid JSON (golden
   diff empty if baseline captured).
7. No `std::to_string`/`std::stoi` in `Manifest.cpp` (T32 uClibc safety).
8. `src/hal/**` untouched. No singleton decoupling (still reads
   `Settings`/`MCU`/`DeviceConfig` internally).

---

## 9. Follow-up (explicitly OUT OF SCOPE for T9)

- **T9-followup: decouple manifest singletons.** Pass `Settings`/`MCU`/
  `DeviceConfig` snapshots (or a `ManifestContext` struct) into
  `generateDescInfo`/`createDescInfoFile` as parameters instead of calling
  `::getInstance()`. This is the real testability win (lets a unit test inject
  fakes) and unblocks a proper unit test for the JSON shape. Do NOT do this in
  T9 — it would change signatures and call sites, violating the
  behavior-preserving gate.

---

## 10. Call-site-by-call-site change list (main_app.cpp)

- `:83-122`  `static bool getFileCreationTime(...)` -> **delete** (moves into Manifest.cpp as file-local static).
- `:268-451` `static int generateDescInfo(...)`      -> **delete** (moves into Manifest.cpp as `manifest::generateDescInfo`).
- `:453-471` `static int createDescInfoFile(...)`    -> **delete** (moves into Manifest.cpp as `manifest::createDescInfoFile`).
- `:456`     internal `generateDescInfo(media_files, desc_info)` -> moves with body (no app edit).
- `:609`     `createDescInfoFile(...)`               -> `manifest::createDescInfoFile(...)`.
- `:621`     `createDescInfoFile(...)`               -> `manifest::createDescInfoFile(...)`.
- `:716`     `generateDescInfo(files, desc_info)`    -> `manifest::generateDescInfo(files, desc_info)`.
- `:790`     `createDescInfoFile(...)`               -> `manifest::createDescInfoFile(...)`.
- add `#include "manifest/Manifest.h"` near the local include block.
