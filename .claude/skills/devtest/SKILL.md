---
name: devtest
description: Drive the T32 devtest loop over serial — build (dual-platform) → NFS deploy (md5-verify) → run the app on the device via the devctl broker → deterministic pytest verdict → present pass/fail + diagnosis to the human. Use when the user says "devtest", "/devtest", "run the device test", "verify on hardware", "wm-repeat", or wants to exercise a change on the real T32. Phase-0 is Level-1 (present, do NOT auto-edit code).
---

# /devtest — T32 device test loop

Automates the "串口手敲 → 看日志 → 存 log → 对照代码" chain over the serial broker.
Full design + 8 decisions: `doc/knowledge/decisions/devtest-automation-loop.md`.

## Prerequisites (check first, fail fast)

1. Broker up: `python3 tools/devctl/broker.py --self-test` then start it —
   `tools/devctl/devctl broker start` (or `python3 tools/devctl/broker.py &`).
   Confirm: `tools/devctl/devctl status`.
2. Device shell reachable: `tools/devctl/devctl run 'uname -a'` returns output + rc 0.
3. NFS mounted with `noac`: the device sees `/mnt/huntcam`. After a cold boot, run
   `HTC_WIFI_PWD=<pwd> tools/devctl/devctl bringup` — it auto-detects home/company
   from the host IP, mounts SD → connects WiFi (right SSID per env) → mounts NFS
   with noac → verifies. After a power-cycle: `tools/devctl/devctl wait-boot` first,
   then `bringup`.

If any prerequisite fails, stop and tell the user exactly what's missing — do NOT proceed.

## Scenario → action

The argument selects a scenario (maps to a `tests/host/test_*.py`). Each drives
the device via `devctl run` + a deterministic verdict (`scenario_verdict.judge`
or `wm_verdict.check_run`).

| scenario | test file | what it asserts |
|---|---|---|
| `wm-repeat` (default) | `test_wm_repeat.py` | `-wm 0` twice in one boot; 2nd run must not hang at rc=137 (IMP residue) |
| `record-smoke` | `test_record_smoke.py` | single `-wm 0`; record started + fps + rc 0, no non-benign E/ |
| `net-smoke` | `test_net_smoke.py` | wlan0 has IPv4 + `htc_net_app` REUSE exits clean |
| `wm-modes-matrix` | `test_wm_modes_matrix.py` | `@parametrize` mode{0,1,2}×rtc{0,1}; each exits clean (crash/hang detector) |

Pure verdict-logic unit tests (`test_verdict.py`, `test_scenario_verdict.py`)
run anywhere — use them to pin the pass/fail contract without a device.

## The loop (run in order)

1. **Tier-0 build (hard gate, both platforms must compile):**
   ```
   cmake --build build     -j$(nproc) --target htc_workmode_app
   cmake --build build_sim -j$(nproc) --target htc_workmode_app
   ```
   If either fails → stop, report the error. Do not deploy broken code.
2. **Deploy + verify** the binary the device will actually run:
   ```
   tools/devctl/verify_deploy.sh htc_workmode_app
   ```
   md5 host == device is mandatory; a mismatch means NFS is serving a stale
   binary (mount needs `noac`).
3. **Run the scenario's pytest** (deterministic verdict) — pick the file from
   the table above, e.g. `record-smoke`:
   ```
   HTC_WIFI_PWD=<pwd> python3 -m pytest tests/host/test_record_smoke.py -s \
       --junit-xml=logs/record_smoke.xml
   ```
   (`HTC_WIFI_PWD` only needed for `net-smoke`.) The verdict logic itself is
   unit-tested in `tests/host/test_verdict.py` + `test_scenario_verdict.py`
   (run those first, no device, to confirm the pass/fail contract hasn't drifted).
   Every session also writes a summary to **`logs/devtest_report.json`**
   (per-test outcome + duration + pass/fail/skip counts) — read it for the
   at-a-glance result.
4. **If a run hangs / the device wedges** (no shell within timeout):
   `tools/devctl/devctl run` returns `timed_out: true` or the broker stops
   answering → **STOP**. Do not auto-retry in a tight loop. Tell the user the
   device likely needs a manual power-cycle (Phase-3 will add a network relay).

## Present the result + Level-2 diagnose/fix loop

State PASS / FAIL with the concrete reason (from the scenario's verdict) and
point at the evidence: `logs/devtest_report.json` (summary), the junit-xml, and
`logs/serial.log`.

On FAIL, run the Level-2 loop — **auto-diagnose, human approves the fix**:
1. **Capture** — `tools/devctl/devctl log -n 300` (serial.log around the failure)
   + the failing test's assertion tail.
2. **Diagnose** — read the serial.log + relevant source (`src/media/video`,
   `src/app/workmode`, …), name the root cause (crash signature / IMP residue /
   missing backend).
3. **Propose** — write a candidate diff + reasoning; surface it to the human.
4. **Approve** — do NOT apply until the human approves (Level-2 boundary). Cap at
   **3 diagnose iterations** per `/devtest` invocation, then stop + report.
5. **Apply + re-run** — after approval: apply → Tier-0 rebuild (both platforms)
   → md5 re-deploy → re-run the scenario → loop to 1.

**Crash/wedge signatures seen so far** (feed the stabilization, don't re-litigate):
- `IMP_Encoder_CreateChn(0) failed` on the 2nd consecutive record — deterministic
  encoder-teardown defect (cold boot does NOT clear it).
- `Unhandled kernel unaligned access` in `initVideo` right after `VTS corrected
  to 0x0690` — dirty-boot (long-uptime) sensor/ISP ISR; cleared by cold boot.

Always leave the device in a known state: if you launched a long app, send
`tools/devctl/devctl ctrl-c`.

## Guardrails (never violate)

- Never run destructive device commands (`mkfs`, `dd`, flash, `rm -rf /`).
- **Never use a bare `exit N` in `devctl run`** — it kills the device's login
  shell, so the trailing sentinel never runs (→ timeout) and a fresh shell
  respawns. To test a non-zero exit code use `(exit 7)` (subshell) or `false`.
- Respect iteration caps: at most 3 test runs per `/devtest` invocation before
  reporting back.
- **Cold-boot discipline**: every `-wm` run touches IMP; residue accumulates
  across runs in one boot and wedges (deterministic `IMP_Encoder_CreateChn` on
  the 2nd record + dirty-boot `initVideo` unaligned access). Until the wm/um
  stabilization lands clean single-process teardown, run **one record-heavy case
  per cold boot**.
- **Wedge = STOP**: if `devctl run` times out and `ctrl-c` doesn't revive the
  shell, the device is hard-wedged (kernel oops). Do NOT auto-retry. Tell the
  user it needs a **manual power-cycle** (serial can only soft-reboot; the
  network relay for automated cold-reset is Phase-3).
- **busybox, not coreutils**: the device has no `head`/`tail`/`tac`. Shell
  snippets inside `devctl run` must use `awk`/`sed`/`grep`-safe sh.
- `logs/` is the shared evidence dir — point the user at the devtest_report.json
  + junit xml + serial log, don't inline giant logs in chat.
