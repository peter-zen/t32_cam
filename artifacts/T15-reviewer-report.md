---
contract: report
contract_version: "1"
task_id: T15
node: reviewer
flow: feature
status: success
summary: |
  APPROVE. Independent final audit of Phase C-2 (extract the command cascade +
  `-wm` switch + 5 helpers from src/app/main_app.cpp into new
  src/app/workmode/WorkModeRunner.{h,cpp}). PRIMARY gate (byte-identity) CONFIRMED
  by extracting HEAD ranges vs WorkModeRunner ranges and `diff -wB` under the
  allowed substitutions: cascade (HEAD 681-1170 ↔ WMR 347-839) diffs to ONLY the
  trailing `return CascadeResult::Continue;` (normal-completion fall-through);
  switch (HEAD 585-613 ↔ WMR 845-874) body identical; all 5 helpers IDENTICAL
  (static→anon-ns only). ZERO hidden logic edit. goto→CascadeResult accounting
  exact: 30 cascade `goto main_exit`→30 `return Continue` (+1 normal completion =
  31) inside runCommands; the invalid-mode `default`→1 `return TerminalExit`.
  Outcome-preservation VERIFIED: normal→tail; any goto→tail; invalid-mode→tail
  skipped (TerminalExit→`return -1`); the commonStartupPostDispatch-fail goto
  stays in main_app (line 314) and still reaches the tail — no outcome changed.
  R1 run-loop rewiring (CMD_MOBILE/CMD_RTSP_SERVER) byte-identical (HEAD already
  used lc.markRtspSingletonUsed/keepRunning/waitForSignal; C2 only adds ctx.lc./
  [&ctx]). R3 by-ref CONFIRMED: mgmtServClient/storageServClient are main()-locals
  held in the ctx as `shared_ptr<...>&` (same instances the cleanupHook captures
  by ref and nulls); the cascade writes them through the ref; lc outlives
  runCommands. ODR CLEAN: CMD_* single-sourced in WorkModeRunner.h:14-27, zero
  duplicate `#define` in src/, main_app gets them via include. R7 T32 link
  CONFIRMED: both sim and T32 `htc_main_app` build+link exit 0 (only 2 inherited
  -Wunused-function warnings for pre-existing normalizePath/parseIniFile). The
  `../service/camera` include dir is needed (CameraRecorder.h). src/hal untouched;
  zero std::to_string/stoi (uses portable to_string_custom/stoi_custom). The
  implementer-noted `config->flush()` null-deref is byte-identical to HEAD
  (pre-existing, currently unreachable) — not a C2 regression. Tier-3 device A/B
  flagged MANDATORY user-run before C4 (sim can't surface R1 next-boot-hang).
deliverables:
  - artifacts/T15-reviewer-evidence.md
  - artifacts/T15-reviewer-report.md
verification:
  commands:
    - git show HEAD:src/app/main_app.cpp
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - grep -nE 'goto main_exit|CascadeResult|runCommands|runWorkMode' src/app/main_app.cpp src/app/workmode/WorkModeRunner.cpp
    - grep -nE 'target_link_libraries|add_library' src/app/workmode/CMakeLists.txt
    - grep -rnE '#define CMD_(CONN_NET|DHCP|SNAP|MOBILE|RTSP_SERVER|NTP|GET_RTC|SET_RTC)' src/
  evidence_ref: artifacts/T15-reviewer-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T15-reviewer-evidence.md
---
