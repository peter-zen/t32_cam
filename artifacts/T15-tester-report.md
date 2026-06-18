---
contract: report
contract_version: "1"
task_id: T15
node: tester
flow: feature
status: success
summary: |
  Gate 1 (sim+T32 build+link): PASS — both `htc_main_app` exit 0 (R7 holds).
  Gate 2 (byte-identity, PRIMARY): PASS — 5 helpers IDENTICAL (static→anon-ns only);
  cascade IDENTICAL under allowed deltas (only a `//RTC` comment + the closing
  `return CascadeResult::Continue;`); switch IDENTICAL under allowed deltas.
  Gate 3 (3-outcome sim smoke A/B vs HEAD): PASS — valid `-wm 0`→exit0+"exit normally",
  invalid `-wm 99`→exit255+tail SKIPPED, `-grtc`→exit0+tail; only delta is the moved
  switch's `__func__` (`main`→`runWorkMode`) in the invalid-mode log.
  Gate 4 (routing): PASS — `runWorkMode` in `-wm` branch, `runCommands` in single-shot.
  Gate 5 (CMD_* single-sourced in WorkModeRunner.h): PASS.
  Gate 6 (cleanupHook by-ref shares ctx instances): PASS.
  Gate 7 (src/hal empty; no std::to_string/stoi): PASS.
  Tier-3 device A/B = USER-RUN (sim can't surface R1 next-boot-hang): flagged, not blocking.
deliverables:
  - artifacts/T15-tester-evidence.md
  - artifacts/T15-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - git show HEAD:src/app/main_app.cpp
    - grep -nE 'runCommands|runWorkMode|CascadeResult' src/app/main_app.cpp src/app/workmode/WorkModeRunner.cpp
  evidence_ref: artifacts/T15-tester-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T15-tester-evidence.md
---
