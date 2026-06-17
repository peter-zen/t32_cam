---
contract: report
contract_version: "1"
task_id: T9
node: implementer
flow: feature
status: success
summary: |
  Extracted the desc-info JSON generation into a new `manifest` SHARED library,
  behavior-preserving (verbatim move). Created `src/manifest/Manifest.h`,
  `src/manifest/Manifest.cpp`, `src/manifest/CMakeLists.txt`. Wired
  `add_subdirectory(manifest)` before `app` in `src/CMakeLists.txt`; added the
  manifest include dir and linked `manifest` into `htc_main_app` in BOTH the
  sim and hw blocks of `src/app/CMakeLists.txt`. In `src/app/main_app.cpp`
  deleted the 3 statics (`getFileCreationTime`, `generateDescInfo`,
  `createDescInfoFile`), added `#include "manifest/Manifest.h"`, and prefixed
  the 4 external call sites with `manifest::`. The moved bodies are
  byte-for-byte identical to `git show HEAD:src/app/main_app.cpp` (verified
  per-function via diff; only `static ` dropped from the two public
  signatures, which now live inside `namespace manifest {}`, and
  `getFileCreationTime` retains its `static` keyword as a file-local).
  Both platforms build + link clean: sim exit 0, T32 exit 0.
  `build_sim/lib/libmanifest.so` (ELF64 x86-64) and
  `build/lib/libmanifest.so` (ELF32 MIPS R3000) both produced.
  Zero `std::to_string`/`std::stoi`/`std::stoul` in Manifest.cpp (T32 uClibc
  safe). `src/hal/**` untouched; no singleton decoupling (out of scope).
  Two CMake-only deviations from the planner were needed to land a clean
  build (documented in state_delta / full archive): (1) added
  `${CMAKE_CURRENT_SOURCE_DIR}/..` to manifest's include_directories so
  `#include "manifest/Manifest.h"` resolves; (2) added `crc16` to manifest's
  PUBLIC link list to resolve `common_utils_crc`'s undefined `cal_crc16`
  (common_utils_crc omits its own crc16 dependency; htc_main_app resolves it
  directly today).
deliverables:
  - src/manifest/Manifest.h
  - src/manifest/Manifest.cpp
  - src/manifest/CMakeLists.txt
  - src/CMakeLists.txt
  - src/app/CMakeLists.txt
  - src/app/main_app.cpp
  - artifacts/T9-implementer-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - ls -l build_sim/lib/libmanifest.so build/lib/libmanifest.so
    - grep -nE 'std::to_string|std::stoi|std::stoul' src/manifest/Manifest.cpp
    - grep -nE 'manifest::(generateDescInfo|createDescInfoFile)' src/app/main_app.cpp
  evidence_ref: src/manifest/Manifest.cpp
state_delta:
  set_task_status: {}
artifact_path: src/manifest/Manifest.cpp
---
