---
contract: report
contract_version: "1"
task_id: T9
node: tester
flow: feature
status: success
summary: |
  Gate 1 (both builds link clean + libmanifest.so on sim x86-64 and T32 MIPS32): PASS.
  Gate 2 (PRIMARY byte-identical body diff vs HEAD:src/app/main_app.cpp): PASS — getFileCreationTime byte-identical; generateDescInfo and createDescInfoFile differ ONLY on the signature line (static int -> int inside namespace manifest), every statement from { to } unchanged.
  Gate 3 (call sites): PASS — exactly 4 manifest:: hits in main_app.cpp, zero leftover static definitions, manifest/Manifest.h included.
  Gate 4 (T32 uClibc safety): PASS — no std::to_string/stoi/stoul.
  Gate 5 (scope): PASS — HAL diff empty; Settings/MCU/DeviceConfig singletons preserved verbatim (decoupling out of scope).
  Gate 6 (CMake): PASS — add_subdirectory(manifest) before app; manifest linked in BOTH sim block and hw block of htc_main_app plus include dir.
  Gate 7 (sim smoke, non-blocking): PASS — htc_main_app -h exits 0.
deliverables:
  - artifacts/T9-tester-evidence.md
  - artifacts/T9-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - git show HEAD:src/app/main_app.cpp
    - grep -nE 'manifest::(generateDescInfo|createDescInfoFile)' src/app/main_app.cpp
    - diff <sed HEAD body> <sed Manifest.cpp body>
  evidence_ref: artifacts/T9-tester-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T9-tester-evidence.md
---
