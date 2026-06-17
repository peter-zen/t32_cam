---
contract: report
contract_version: "1"
task_id: T11
node: tester
flow: feature
status: success
summary: |
  Gate 1 PASS: both build_sim and build (T32 uClibc) htc_main_app exit 0.
  Gate 2 PASS: NTP moved body byte-identical (whitespace-normalized diff of HEAD
  CMD_NTP loop vs Misc::ntpSyncAndWait — every logic line identical; only diff =
  two trailing structural close-braces + allowed signature/goto->return/indent).
  Gate 3 PASS: all 4 mDNS helpers + trimConfigString byte-identical (normalized
  diff exit 0 each; allowed static->service:: namespace wrap only).
  Gate 4 PASS: exactly 1 ntpSyncAndWait call (main_app.cpp:1110); 2 service::
  mDNS call sites (1264-1265); RTC writeback present (1114-1117).
  Gate 5 PASS (critical): CMD_RTSP_SERVER block byte-identical to HEAD (raw diff
  empty); CMD_MOBILE region differs from HEAD ONLY at the two service:: prefixes;
  no RtspWorkMode files exist. RTSP deferred to T12, untouched.
  Gate 6 PASS: trimConfigString (def 85, used 145) + getConfiguredPort (def 153,
  used 1230/1231/1338) retained.
  Gate 7 PASS: src/hal diff empty; no singleton decoupling.
  Gate 8 PASS: no std::to_string/stoi/stoul in moved bodies.
deliverables:
  - artifacts/T11-tester-evidence.md
  - artifacts/T11-tester-report.md
verification:
  commands:
    - cmake --build build_sim -j$(nproc) --target htc_main_app
    - cmake --build build -j$(nproc) --target htc_main_app
    - git show HEAD:src/app/main_app.cpp
    - grep -nE 'ntpSyncAndWait|service::buildMdnsParams' src/app/main_app.cpp
  evidence_ref: artifacts/T11-tester-evidence.md
state_delta:
  set_task_status: {}
artifact_path: artifacts/T11-tester-evidence.md
---
