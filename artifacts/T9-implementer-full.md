# T9 implementer — full archive

task_id: T9, node=implementer, flow=feature, status=success
branch: feature/new-workmode

## 1. What was built

A new `manifest` SHARED library holding the desc-info JSON generation,
extracted verbatim from `src/app/main_app.cpp`. Behavior-preserving move;
singletons (`Settings`/`MCU`/`DeviceConfig`) still read internally —
decoupling is explicitly out of scope (T9-followup).

### New files

- `src/manifest/Manifest.h` — `namespace manifest { ... }` with two public
  signatures identical to today (`generateDescInfo`, `createDescInfoFile`).
  `getFileCreationTime` is intentionally NOT exposed (stays file-local
  `static` in the .cpp).
- `src/manifest/Manifest.cpp` — `getFileCreationTime` (file-local `static`),
  `generateDescInfo`, `createDescInfoFile`, all wrapped in
  `namespace manifest {}`. Bodies byte-identical to HEAD (see §4).
- `src/manifest/CMakeLists.txt` — SHARED lib `manifest`, PUBLIC-links
  jsoncpp / common_misc / common_utils_crc / crc16 / common_time_timezone /
  setting / devconf / mcu / disk / logger; `LIBRARY_OUTPUT_DIRECTORY
  ${CMAKE_BINARY_DIR}/lib`. Includes `${CMAKE_CURRENT_SOURCE_DIR}/..` so the
  bare `"manifest/Manifest.h"` include resolves (deviation, see §5).

### Edited files

- `src/CMakeLists.txt` — added `add_subdirectory(manifest)` immediately
  before `add_subdirectory(app)`.
- `src/app/CMakeLists.txt`
  - added `${CMAKE_CURRENT_SOURCE_DIR}/../manifest` to the
    `include_directories(...)` block.
  - added `manifest` to `htc_main_app`'s PRIVATE link list in BOTH the
    `if(BUILD_FOR_SIMULATION)` block (after `app_workmode`, before `logger`)
    and the `else()` hw block (same position).
- `src/app/main_app.cpp`
  - deleted the 3 statics (`getFileCreationTime`, `generateDescInfo`,
    `createDescInfoFile`).
  - added `#include "manifest/Manifest.h"` after `CameraFactoryConfigImporter.h`.
  - prefixed the 4 external call sites with `manifest::`:
    - `manifest::createDescInfoFile(file_names, desc_filename);` (processCmdSnap)
    - `manifest::createDescInfoFile(file_names, "./res/20250620_101358.json");`
    - `manifest::generateDescInfo(files, desc_info)` (processCmdVideoRecord)
    - `manifest::createDescInfoFile(all_files, desc_filename);` (processCmdConcurrentSnapRecord)
  - removed no other includes (shared with other code in the file).

## 2. Build evidence (both platforms, clean)

```
$ cmake --build build_sim -j$(nproc) --target htc_main_app
exit=0
$ cmake --build build -j$(nproc) --target htc_main_app      # toolchain.cmake (T32)
exit=0
$ ls -l build_sim/lib/libmanifest.so build/lib/libmanifest.so
-rwxrwxr-x ... 521256 ... build_sim/lib/libmanifest.so   # ELF64 x86-64
-rwxrwxr-x ...  27772 ... build/lib/libmanifest.so       # ELF32 MIPS R3000
$ ls -l build_sim/bin/htc_main_app build/bin/htc_main_app
-rwxrwxr-x ... 15293608 ... build_sim/bin/htc_main_app
-rwxrwxr-x ...   616340 ... build/bin/htc_main_app
```

ELF arch confirmed via `readelf -h`:
- `build_sim/lib/libmanifest.so` → Machine: Advanced Micro Devices X86-64
- `build/lib/libmanifest.so`       → Machine: MIPS R3000

## 3. Gate checks

- `grep -nE 'std::to_string|std::stoi|std::stoul' src/manifest/Manifest.cpp`
  → **zero hits** (T32 uClibc safe). Body uses `to_string_custom` /
  `snprintf` / `atoi` only.
- `grep -cE 'manifest::(generateDescInfo|createDescInfoFile)' src/app/main_app.cpp`
  → **4** (the 4 external call sites).
- `grep -nE '^static (int|bool) (generateDescInfo|createDescInfoFile|getFileCreationTime)' src/app/main_app.cpp`
  → **empty** (no static defs remain in main_app.cpp).
- `git status --short src/hal/` → **empty** (hal untouched).
- No singleton decoupling (still calls `Settings::getInstance()` etc.
  internally — unchanged from HEAD).

## 4. Byte-identical body verification (reviewer gate §6b)

For each of the three functions, extracted the body from
`git show HEAD:src/app/main_app.cpp` and from the new
`src/manifest/Manifest.cpp`, then diffed after normalizing the signature
line (dropping `static ` and `manifest::`):

```
getFileCreationTime: BYTE-IDENTICAL   (both keep `static` — file-local by design)
generateDescInfo:    BYTE-IDENTICAL   (only `static ` dropped; now inside namespace)
createDescInfoFile:  BYTE-IDENTICAL   (only `static ` dropped; now inside namespace)
```

Note on the `manifest::` qualifier: the two public function *definitions*
live inside `namespace manifest { ... }`, so they are written as plain
`int generateDescInfo(...)` / `int createDescInfoFile(...)`. Writing
`int manifest::generateDescInfo(...)` *inside* the namespace is illegal C++
("explicit qualification in declaration") — confirmed by an initial build
failure, then fixed. The internal call
`generateDescInfo(media_files, desc_info)` inside `createDescInfoFile`
resolves unqualified within the namespace, as the planner §3 note predicted.

## 5. Deviations from the planner (CMake/link only — no body/logic change)

Two additions to `src/manifest/CMakeLists.txt` were necessary to land a
clean link on both platforms. Neither touches any function body.

1. **Include path for the public header.** The planner's CMakeLists added
   `${CMAKE_CURRENT_SOURCE_DIR}` (i.e. `src/manifest/`) to the include path,
   but the source uses `#include "manifest/Manifest.h"`, which needs `src/`
   on the path. Added `${CMAKE_CURRENT_SOURCE_DIR}/..` to
   `include_directories(...)`. Without this: `fatal error:
   manifest/Manifest.h: No such file or directory`.
2. **`crc16` link dependency.** `common_utils_crc` (which provides
   `CRC::calculate_crc16`) internally calls `cal_crc16` (provided by the
   `crc16` target in `third_party/crc16`), but `common_utils_crc`'s own
   CMakeLists links only `logger` PRIVATE — it omits `crc16`. Today
   `htc_main_app` resolves `cal_crc16` because it links `crc16` directly.
   When `manifest` PUBLIC-links `common_utils_crc`, the linker first hits
   `libcommon_utils_crc.so`'s undefined `cal_crc16` and fails before the
   app's own `crc16` link can satisfy it
   (`libcommon_utils_crc.so: undefined reference to cal_crc16`). Added
   `crc16` to `manifest`'s PUBLIC link list so the symbol resolves
   transitively. This is a real fix for a latent missing-dep in
   `common_utils_crc` surfaced by the new transitive consumer; it does not
   change behavior (the app already pulled `crc16` in).

Both deviations are additive CMake changes only; they introduce no new code
paths and no behavior change.

## 6. Out of scope (confirmed not done)

- Singleton decoupling (Settings/MCU/DeviceConfig still read via
  `::getInstance()` inside the lib) — deferred to T9-followup per planner §9.
- No changes to `htc_media_app` / `htc_daemon_app` / `htc_wifi_app` /
  `snap_test` (they don't call these functions).
- No `src/hal/**` changes.

## 7. Rollback

Pure additive extraction. `git revert` of the T9 commit restores the 3
statics in `main_app.cpp` and removes `src/manifest/`. No data/config/state
migration.
