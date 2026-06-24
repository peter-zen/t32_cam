# 2026-06-22 — scenario-test-manifest backlog (B1–B7): implementation + HW status

Implements the 7 not-yet-implemented scenario tests from
[`doc/knowledge/specs/scenario-test-manifest.md`](../doc/knowledge/specs/scenario-test-manifest.md)
(synced this session, commit `0cdc58c`). Two layers: L1 sim (`tests/*.cpp`) and
L2 real-hardware (`tests/host/*.py` via the devctl broker).

## Summary — all GREEN or honestly documented

| Item | File | Layer | Status |
|------|------|-------|--------|
| **B5** ntp | `tests/test_ntp.cpp` | sim | ✅ GREEN (build_sim + run) |
| **B6** http-api | `tests/test_http_api.cpp` | sim | ✅ GREEN (build_sim + run) |
| **B2** upload | `tests/host/test_upload_smoke.py` | HW | ✅ GREEN on device (27.7s) |
| **B3** mobile-um (wm3) | `tests/host/test_um_mobile_probe.py` | HW | ✅ GREEN on device (60.8s) |
| **B4** rtsp-um (wm4) | `tests/host/test_um_rtsp_probe.py` | HW | ✅ GREEN on device (60.6s) |
| **B7** wm-matrix | `tests/host/test_wm_modes_matrix.py` | HW | ✅ wm2-rtc1 GREEN; wm0 record proven by `test_record_smoke`; wm1 anchors captured (record+upload). Per-mode verdict upgrade landed. |
| **B1** snap | `tests/host/test_snap_smoke.py` | HW | ✅ runs via `snap_test` (5-case matrix GREEN 2026-06-23; 8m-burst OOM fixed — see [2026-06-23 review](2026-06-23-snap-burst-oom-fix.md)) |

Pure verdict unit tests (`test_verdict.py`, `test_scenario_verdict.py`): 23 passed, contract undisturbed.

## What landed cleanly

### B5 — `tests/test_ntp.cpp` (sim, GREEN)
`syncSystemTime()` isn't cleanly sim-testable (host clock ≥2026 → early-outs;
RTC/MCU have no sdk_stub). The meaningful sim test locks the decision logic
directly: `TIME_PLAUSIBLE` (`Common.h:215`, threshold 2026) on synthetic `tm`;
`RTC::setTime` field validation + graceful-failure (fd==-1); MCU
implausible-in-sim. Links `common_time_rtc`+`mcu` (light — NOT `app_lifecycle`).
Mirrors `test_net_app_logic.cpp` EXPECT_* idiom. `ALL PASS`.

### B6 — `tests/test_http_api.cpp` (sim, GREEN)
Handlers in `http_api_v1.cpp` are `static` → only the localhost client/server
idiom works (bind CivetWeb to :18080 in-process, raw-socket HTTP GETs). Covers
probes (`/api/health`, `/healthz`), control (`/api/v1/device/info`,
`/api/v1/camera/status`), method gate (POST→405), and the **playback_token**
resolution path (`?token=bogus`→404, no-args→400, POST→405) — the manifest's key
untested surface, no media-DB seeding needed. `ALL PASS`.

### B2 — `tests/host/test_upload_smoke.py` (HW, GREEN)
`-wm 2` (UPLOAD_ONLY). Real anchors (2026-06-22 scout): `mgmtServerAddr:` →
connect → `auth failed` (devtest device rejected by the real mgmt server) →
Continue → clean exit. Verdict requires `mgmtServerAddr:`, benigns
`auth failed`/`connect [...] failed`, rc 0.

### B3 — `tests/host/test_um_mobile_probe.py` (HW, GREEN) + B4 (wm4, GREEN)
wm3/wm4 are long-running services (`while(keepRunning())`); the test backgrounds
the app, proves the services came up, then SIGINTs for teardown. Verdict:
- HARD: HTTP port 80 listening in /proc/net/tcp (wm3) + the service-up anchors
  in `/mnt/sdcard/logs/app.log` (the app's EasyLogger file sink — reliable; the
  broker's serial capture is NOT, across boots).
- HARD: device shell responsive after SIGINT (not wedged).

**Three device realities the tests had to crack (the hard-won part of B3/B4):**
1. **busybox `grep` stops at the first NUL in `/proc/<pid>/cmdline`.** A cleanup
   matching `-wm 3` (AFTER the NUL) silently matches nothing → lingering wm3
   survives → holds **port 80** → the next instance's `http_server_start` fails
   with `E/LEGACY Failed to start HTTP server` and the cascade aborts. Fix:
   cleanup matches the basename `workmode_app` (BEFORE the NUL).
2. **app.log is truncated on each app startup**, so a before/after anchor DELTA
   is meaningless (before counts the previous app's log, after the new app's).
   Fix: the test truncates app.log itself before spawn → any anchor found is
   provably from THIS run.
3. **The T32 is 32MB RAM / ~2MB free + 16MB zram swap** (`MemTotal 33144 kB`).
   wm3/wm4 (mDNS+HTTP+RTSP+video) swap-thrash their init — **~38s spawn→anchors**
   (vs <1s on a memory-healthy boot). `_WARMUP_S=45s`; teardown (`_TEARDOWN_S=12s`)
   is slow under swap too.

With those fixed, wm3 came up cleanly (`HTTP server started on port 80`, `RTSP
server started on port 8554`, port 80 LISTEN) and SIGINT tore it down cleanly
(shell survived). wm4 likewise (`RTSP server started on port 8554`). Both GREEN.

### B7 — `test_wm_modes_matrix.py` upgraded
Replaced `_BENIGN_ALL=(r".*",)` with per-mode verdicts: wm0 reuses
`wm_verdict.check_run` (+`HTC_SIM_PIR_INTERVAL_MS=999999`), wm1 requires
`record start:`+`mgmtServerAddr:` (device is cameraMode=video-only → wm1 records),
wm2 requires `mgmtServerAddr:`. wm2-rtc1 validated GREEN. Still 1-case-per-cold-boot.

## B1 — snap (re-scoped 2026-06-22, fully validated 2026-06-23)
The initial "production snap unreachable → skip" scoping was wrong (see
[2026-06-22-snap-unification.md](2026-06-22-snap-unification.md)): snap is a
**capture component** (`ImageSnap`) directly drivable via the `snap_test` harness,
independent of the pin-driven `quick_snap`/`htc_media_app` orchestration.
`test_snap_smoke.py` was rewritten to drive `snap_test snap <W> <H> [count]
[--no-thumb]` across a 5-case spec matrix (4m/8m/16m single, 8m-burst,
4m-nothumb) and regex-verify its `[snap_test] result=OK` anchor.

2026-06-23 full HW run (1-boot-1-case): **4m/8m/16m single + 8m-burst all GREEN**
— but 8m-burst OOM-killed flakily on one boot. Root-caused (5.5MB full-frame nv12
staging vector vs the 32MB budget, ~1.7MB margin) and fixed via
`LargeImageSnap::snapLargeFromFile` (strip-by-strip `fseek`+`fread` from the temp
file — no full-frame vector). See
[2026-06-23-snap-burst-oom-fix.md](2026-06-23-snap-burst-oom-fix.md). Production
`quick_snap` gaps (no `sensorNativeSize`, no thumbnail persist) remain out of scope.

The wm1 record+upload path is covered by the matrix[wm1] + `test_upload_smoke.py`.

## Device quirks found (for future devtest runs)
- **Soft reboot breaks SDIO WiFi**: `devctl reboot` (=`send "reboot\n"`) doesn't
  re-enumerate the 8189fs WiFi module — `wlan0` vanishes. Recover with
  `rmmod 8189fs; insmod /system/bin/wifi/8189fs.ko` then `bringup`. A physical
  power-cycle re-enumerates WiFi normally. (Phase-3 relay would automate cold-boot.)
- **busybox is minimal**: no `nohup`, `head`, `tail`, `sed`, `wc`; `grep` on
  `/proc/<pid>/cmdline` stops at the first NUL (match the binary basename only).
- **`devctl run` reuses one persistent serial shell** — background children (`&`)
  survive across runs, but EasyLogger output goes to its **file sink**
  (`/mnt/sdcard/logs/app.log`, truncated each app start), not reliably to the
  serial console capture. Read app.log for anchors; truncate it first for a
  per-run-scoped check.
- **32MB RAM / ~2MB free + 16MB zram swap** — memory-heavy services (wm3/wm4)
  swap-thrash; allow ~45s warmup.

## Files
- New: `tests/test_ntp.cpp`, `tests/test_http_api.cpp`,
  `tests/host/test_snap_smoke.py`, `tests/host/test_upload_smoke.py`,
  `tests/host/test_um_mobile_probe.py`, `tests/host/test_um_rtsp_probe.py`.
- Edited: `tests/CMakeLists.txt` (register test_ntp/test_http_api),
  `tests/host/test_wm_modes_matrix.py` (B7 per-mode anchors).
- No `src/` changes; no `src/hal/**` touched. No commit (project git policy).

## Next (optional)
- B7 wm0/wm1 + B1 full matrix: **done 2026-06-23** (wm0-rtc0, wm1-rtc0 GREEN; snap
  5-case matrix GREEN; 8m-burst OOM fixed). rtc1 variants skipped (rtc is a no-op
  for the record/upload anchors).
- Carry the unified snap path into `quick_snap`/`takePhoto` (production gaps:
  set `sensorNativeSize`, persist thumbnail).
