# T9 — Tester Evidence (feature flow)

Task: verify behavior-preserving extraction of `generateDescInfo`, `createDescInfoFile`,
`getFileCreationTime` from `src/app/main_app.cpp` into a new `src/manifest/` SHARED lib
(`namespace manifest::`), linked into `htc_main_app` on both sim and T32.

Branch under test: `feature/new-workmode` (worktree `new-workmode`).
Baseline for byte-diff: `HEAD:src/app/main_app.cpp` (1894 lines).

---

## Gate 1 — Both platforms build+link clean: PASS

### 1a. Sim build
Command: `cmake --build build_sim -j$(nproc) --target htc_main_app`
Exit: **0**

Relevant tail:
```
Consolidate compiler generated dependencies of target manifest
...
[ 80%] Built target manifest
...
[100%] Built target htc_main_app
```

### 1b. T32 cross build
Command: `cmake --build build -j$(nproc) --target htc_main_app`
Exit: **0**

Relevant tail:
```
Consolidate compiler generated dependencies of target manifest
[ 79%] Built target manifest
...
[100%] Built target htc_main_app
```

### 1c. libmanifest.so artifacts exist
```
-rwxrwxr-x 1 zengping zengping  27772 Jun 17 10:22 build/lib/libmanifest.so
-rwxrwxr-x 1 zengping zengping 521256 Jun 17 10:22 build_sim/lib/libmanifest.so

build_sim/lib/libmanifest.so: ELF 64-bit LSB shared object, x86-64, ... with debug_info
build/lib/libmanifest.so:     ELF 32-bit LSB shared object, MIPS, MIPS32 rel2 version 1 (SYSV), stripped
```

---

## Gate 2 — Byte-identical body diff (PRIMARY behavior-preservation gate): PASS

Method: extracted the original function bodies from `git show HEAD:src/app/main_app.cpp`
and diffed them line-for-line against the corresponding bodies in
`src/manifest/Manifest.cpp`.

Ranges extracted:
- HEAD `getFileCreationTime`: lines 83-122  vs  Manifest.cpp lines 27-66
- HEAD `generateDescInfo`:    lines 268-451 vs  Manifest.cpp lines 68-251
- HEAD `createDescInfoFile`:  lines 453-471 vs  Manifest.cpp lines 253-271

### 2a. `getFileCreationTime` — byte-identical
```
diff /tmp/orig_getFileCreationTime.txt /tmp/new_getFileCreationTime.txt
(empty — exit 0)
```
Keeps `static` (private TU-local helper inside `namespace manifest {}`), exactly as required.

### 2b. `generateDescInfo` — signature-only diff, body byte-identical
```
diff /tmp/orig_generateDescInfo.txt /tmp/new_generateDescInfo.txt
1c1
< static int generateDescInfo(std::vector<std::string>& files, std::string& desc_info)
---
> int generateDescInfo(std::vector<std::string>& files, std::string& desc_info)
```
Only the signature line differs (`static int` → `int` inside `namespace manifest {}`).
Every statement from the opening `{` to closing `}` is byte-identical. **Allowed diff.**

### 2c. `createDescInfoFile` — signature-only diff, body byte-identical
```
diff /tmp/orig_createDescInfoFile.txt /tmp/new_createDescInfoFile.txt
1c1
< static int createDescInfoFile(std::vector<std::string>& media_files, const std::string &desc_filename)
---
> int createDescInfoFile(std::vector<std::string>& media_files, const std::string &desc_filename)
```
Only the signature line differs (`static int` → `int`). Body byte-identical. **Allowed diff.**

**Verdict: zero substantive body changes across all three functions.**

---

## Gate 3 — Call sites correct: PASS

### 3a. `manifest::` call sites in main_app.cpp (expect exactly 4)
```
$ grep -nE 'manifest::(generateDescInfo|createDescInfoFile)' src/app/main_app.cpp
364:        manifest::createDescInfoFile(file_names, desc_filename);
376:        manifest::createDescInfoFile(file_names, "./res/20250620_101358.json");
471:    if (manifest::generateDescInfo(files, desc_info) == 0) {
545:    manifest::createDescInfoFile(all_files, desc_filename);
```
Count = **4** (matches expected ~609/621/716/790 from planner, with line numbers shifted by
the deletion of the three definitions). PASS.

### 3b. Leftover static definitions (expect ZERO)
```
$ grep -nE 'static .*(generateDescInfo|createDescInfoFile|getFileCreationTime)' src/app/main_app.cpp
(empty — exit 1)
```
PASS. No bare definition references remain either:
```
$ grep -nE '^(static )?(int|bool) (generateDescInfo|createDescInfoFile|getFileCreationTime)' src/app/main_app.cpp
(empty — exit 1)
```

### 3c. Header include present
```
$ grep -nE 'manifest/Manifest.h' src/app/main_app.cpp
65:#include "manifest/Manifest.h"
```

---

## Gate 4 — T32 uClibc safety: PASS
```
$ grep -nE 'std::to_string|std::stoi|std::stoul' src/manifest/Manifest.cpp
(empty — exit 1)
```
No forbidden std::string conversions. Uses `snprintf`/`atoi` (uClibc-safe).

---

## Gate 5 — Scope boundaries: PASS

### 5a. HAL untouched
```
$ git diff --stat HEAD -- src/hal
(empty)
```

### 5b. Singletons still read inside Manifest.cpp (OUT OF SCOPE decoupling — presence correct)
```
$ grep -nE '::getInstance\(\)' src/manifest/Manifest.cpp
20:#include "Settings.h"                     // Settings::getInstance()
21:#include "MCU.h"                          // MCU::getInstance()
22:#include "DeviceConfig.h"                 // DeviceConfig::getInstance()
70:    auto settings = Settings::getInstance();
72:    auto mcu = MCU::getInstance();
116:        device_obj["PID"] = DeviceConfig::getInstance()->get(INI_SECTION_DEVICE, INI_KEY_PID, "");
205:        network_obj["N_UPID"] = DeviceConfig::getInstance()->get(INI_SECTION_SYS, INI_KEY_UPID, "CKVISON");
228:            auto program_type = DeviceConfig::getInstance()->get(INI_SECTION_BOOT, INI_KEY_PTYPE, 0);
```
Settings/MCU/DeviceConfig singleton coupling preserved verbatim — expected, decoupling is
an explicit out-of-scope follow-up per the planner.

---

## Gate 6 — CMake wiring: PASS

### 6a. src/CMakeLists.txt — add_subdirectory(manifest) before app
```
$ grep -n 'add_subdirectory(manifest)' src/CMakeLists.txt
37:add_subdirectory(manifest)
```
Line 37, immediately before `add_subdirectory(app)` at line 39. Correct ordering.

### 6b. src/app/CMakeLists.txt — manifest in BOTH sim and hw blocks + include dir
```
$ grep -n 'manifest' src/app/CMakeLists.txt
44:        ${CMAKE_CURRENT_SOURCE_DIR}/../manifest        # include dir
137:        manifest                                       # SIM htc_main_app link
291:        manifest                                       # HW  htc_main_app link
```
Count = 3 (1 include dir + 2 link entries, one per platform block). The two link entries sit
inside the `if(BUILD_FOR_SIMULATION)` block (line 137, in the `htc_main_app` target at 104)
and the `else()` block (line 291, in the `htc_main_app` target at 254) respectively. PASS.

---

## Gate 7 — Sim smoke (best-effort, non-blocking): PASS
```
$ timeout 10 ./build_sim/bin/htc_main_app -h
I/elog   ... EasyLogger V2.2.99 is initialize success.
I/LEGACY ... [SIM] Simulation Root: .../sim_sdcard_runtime
...
Usage: ./build_sim/bin/htc_main_app <command> [options]
Commands:
  -w, --wifi ...
  -h, --help ...
  ...
=== smoke exit: 0 ===
```
App links, initializes EasyLogger, and prints usage cleanly. The byte-identical body diff
(Gate 2) is the real behavior-preservation guarantee; sim runtime exercised only for
link/init sanity.

---

## Summary table
| Gate | Result |
|------|--------|
| 1 — Both builds link clean + .so exist | PASS |
| 2 — Byte-identical body diff (PRIMARY) | PASS |
| 3 — Call sites correct, statics removed | PASS |
| 4 — uClibc safety | PASS |
| 5 — HAL untouched, singletons preserved | PASS |
| 6 — CMake wiring (both blocks + ordering) | PASS |
| 7 — Sim smoke (non-blocking) | PASS |

All gates PASS. Behavior preserved.
