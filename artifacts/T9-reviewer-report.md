---
contract: report
contract_version: "1"
task_id: T9
node: reviewer
flow: feature
status: success
summary: |
  APPROVE. T9 Phase B-1 (behavior-preserving extraction of generateDescInfo +
  createDescInfoFile + file-local getFileCreationTime from main_app.cpp into a new
  SHARED lib src/manifest/, namespace manifest::) is correct and sound on all 7
  review axes. Independently re-ran the body-diff vs HEAD: all 3 function bodies
  are byte-identical (only `static int`->`int` signature lines + namespace wrapper
  changed; zero logic drift; getFileCreationTime correctly stayed `static`). Both
  builds link libmanifest.so as a NEEDED dep of htc_main_app and the app references
  the namespace-mangled symbols; no ODR collision. src/hal untouched; singletons
  still read via ::getInstance() (no decoupling, per plan). Zero uClibc-hostile
  std usage.

  crc16 judgment: the implementer's claim is CORRECT and independently verified.
  common_utils_crc (src/common/utils/crc/CMakeLists.txt) calls cal_crc16
  (CRC.cpp:31) but links only `logger`, never `crc16` — a real latent missing
  dependency that was masked because htc_main_app links crc16 directly. Adding
  `crc16` to manifest's PUBLIC link list is an additive, correct workaround and
  the resulting binaries are sound (libcrc16.so is NEEDED of libmanifest.so on
  both T32 and SIM). The architecturally-proper fix belongs in
  common_utils_crc's own CMakeLists (target_link_libraries(common_utils_crc
  PUBLIC logger crc16)) so the dep is declared at its source; registered as a
  follow-up risk, NOT a T9 blocker.

  Scope note (not a blocker): the working tree also carries unrelated changes
  (res/config.sim.ini swap, doc/knowledge/working-set.md note,
  orchestration-state.yaml) that are sim/doc/orchestration scaffolding and are
  NOT part of the T9 code delta — T9's code change is exactly src/manifest/*
  (new) + src/CMakeLists.txt + src/app/CMakeLists.txt + src/app/main_app.cpp.
deliverables:
  - artifacts/T9-reviewer-evidence.md
  - artifacts/T9-reviewer-report.md
verification:
  commands:
    - git show HEAD:src/app/main_app.cpp
    - readelf -d build/lib/htc_main_app | grep -i manifest
    - readelf -d build_sim/bin/htc_main_app | grep -i manifest
    - grep -nE 'cal_crc16' src/common/utils/crc/CRC.cpp
    - grep -n crc16 src/common/utils/crc/CMakeLists.txt src/manifest/CMakeLists.txt
  evidence_ref: artifacts/T9-reviewer-evidence.md
state_delta:
  add_risk:
    - "common_utils_crc (src/common/utils/crc/CMakeLists.txt) depends on cal_crc16 from the crc16 lib but does not link it; latent missing-dep masked because htc_main_app links crc16 directly. Follow-up: declare `target_link_libraries(common_utils_crc PUBLIC logger crc16)` at the source so consumers inherit it transitively instead of each consumer re-adding crc16 (as manifest now does)."
  set_task_status: {}
artifact_path: artifacts/T9-reviewer-evidence.md
---
