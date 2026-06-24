# 2026-06-22 — Unified snap feature + B1 photo/thumbnail test

> **2026-06-23 update:** the F3 burst path's Phase 2 (load full NV12 into RAM →
> strip-encode via `snapLargeFromBuffer`) was memory-marginal and OOM-killed
> flakily on the 32MB board. Replaced with `LargeImageSnap::snapLargeFromFile`
> (strip-by-strip `fseek`+`fread` from the temp file, no full-frame vector) and
> validated the full 5-case matrix GREEN. See
> [2026-06-23-snap-burst-oom-fix.md](2026-06-23-snap-burst-oom-fix.md).

Follow-up to [scenario-test-backlog.md](2026-06-22-scenario-test-backlog.md) B1.
The B1 "documented skip" was mis-scoped: snap is a **capture component**
(`ImageSnap`) testable directly via `snap_test`, not the pin-driven `quick_snap`/
`htc_media_app` orchestration. On re-examination the current snap code had real
gaps vs the intended spec, and one path was dead on the 32MB board.

## What changed (src/media/snap + src/app/snap_test + B1)

### The spec (the user's), and how the code now meets it
1. **Variable main resolution, ≤8M/>8M auto-route, >8M → strip.** `snap()` keeps
   auto-routing by `isLargeImage_` (target > 3840×2160). The **dead full-frame
   SIMD `snap_large_internal` is replaced by the strip path** (`LargeImageSnap`):
   at 48M the full-frame path needed ~143MB (NV12+JPEG) on a 32MB board —
   `VbmAlloc(71.7MB)` cannot succeed. Strip is memory-safe (~8MB/strip, JPEG
   assembled via DRI/RST markers). Confirmed equivalence: `LargeImageSnap::
   resizeStrip` calls the same `opencv_resize_crop_simd` at the same target res.
2. **Optional thumbnail.** New `ImageSnapParams::setThumbnailEnabled(bool)`
   (default true = byte-identical for `quick_snap`/`takePhoto`). When disabled,
   CH2 isn't opened at all (`initialize()`), not just the capture skipped.
3. **>8M burst temp-buffer flow.** Software scale-up can't keep up with burst
   rate, so for `count>1` the >8M path now: **Phase 1** capture N sensor-native
   NV12 frames to `/mnt/sdcard/.snap_tmp/frame_<i>.nv12` (tight `GetFrameEx`
   loop at sensor rate), **Phase 2** sequentially strip-scale+JPEG-encode each
   via the new `LargeImageSnap::snapLargeFromBuffer(nv12, …)` (the strip loop
   extracted from `snapLarge`, frame-source-agnostic).

### Files
- `src/media/snap/ImageSnap.h` — `ImageSnapParams::enableThumbnail` + accessors;
  `snap_large_burst_internal` decl.
- `src/media/snap/ImageSnap.cpp` — F1 thumbnail gate; F2 `snap_large_internal`
  rewritten on strip (full-frame SIMD + `simd_resize_large` include removed);
  F3 `snap_large_burst_internal` (two-phase temp-buffer flow); `snap()` dispatch
  routes >8M burst to the new flow.
- `src/media/snap/LargeImageSnap.h`/`.cpp` — `snapLargeFromBuffer` extracted;
  `snapLarge` is now a thin GetFrameEx→`snapLargeFromBuffer`→ReleaseFrameEx.
- `src/app/snap_test.cpp` — new `snap <W> <H> [quality] [count] [--no-thumb]
  [outDir]` mode driving the unified `snap()`; emits one machine-parseable
  result anchor (`result/large/count/thumb/files/minbytes/maxbytes/total`).
- `tests/host/test_snap_smoke.py` — rewritten from the skip: drives `snap_test`
  across the 5-case spec matrix, regex-verifies the anchor.

Dual-platform: both build (sim + T32/uClibc). Note: uClibc has no `std::`
qualifier for C stdio — use bare `snprintf`/`fopen`/`fwrite`/… (the existing
code already does).

## HW validation (2026-06-22, broker + device)
`snap_test snap` run directly on the device (NFS-deployed, md5-verified):
| case | result | bytes | total |
|---|---|---|---|
| `snap 3840 2160 85 1` (≤8M) | OK, large=0, thumb=saved | 448922 | 11067ms |
| `snap 5120 2880 85 1` (>8M strip, F2) | OK, large=1, thumb=saved | 1240043 | 3166ms |
| `snap 5120 2880 85 3` (>8M burst temp-buffer, F3) | OK, large=1, count=3, thumb=saved, files=3 | ~1.23MB each | 8078ms |

So the 3 core paths (≤8M, >8M strip, >8M burst temp-buffer) + thumbnail capture
are GREEN on HW. The full 5-case pytest (`8m-single/8m-burst/16m-single/16m-burst
/8m-nothumb`) is **1-boot-1-case**: a 4th snap_test IMP process in one boot
triggers the cross-process IMP wedge (same hardware limit as `test_wm_repeat`);
3 processes per cold boot is the observed safe budget.

## Out of scope (flagged)
- `quick_snap` (media_app) production gaps predate this: it doesn't set
  `sensorNativeSize` (so >8M via `media_app` is still unsafe) and never calls
  `saveThumbnail` (production photos have no DB thumbnail). The optional-thumb
  flag + unified strip path are wired into `ImageSnap`; `quick_snap`/`takePhoto`
  behavior is byte-identical (default thumbnail on) until those are separately
  addressed.
- No `src/hal/**` touched (PIC-owned). No commit (project git policy).

## Next
- Validate the 2 minor variants (`8m-burst`, `8m-nothumb`) on a fresh cold boot
  (the device is currently wedged from the matrix run; needs a power-cycle).
- Optionally carry the unified path into `quick_snap` (set sensorNativeSize +
  persist thumbnail) to close the production gaps above.

## Root cause + fix (confirmed 2026-06-22 via controlled experiments)

The earlier "snap_test needs IMP priming" framing was **wrong** — it was two
independent bugs whose effects happened to vanish on the "primed" boot (its RTC
was already 2026 from a prior NTP, and those runs didn't hit an up-scale target).
Both root causes are now confirmed and fixed; `snap_test` is self-sufficient on a
fresh cold boot (no priming, no manual clock). Methodology lesson: capture FULL
serial output (not keyword grep), and when a crash point is unclear, add
unbuffered debug tracing (stderr+fflush survives a segfault) — don't treat it as
a black box.

### #1 — fresh-boot clock 1970 → `fopen` kernel FAT oops
- **Symptom:** on a fresh boot the RTC is `1970` (unreliable); `snap_test`'s
  `fopen` of the output JPEG → `Unable to handle kernel paging request at
  ffff8080` (kernel oops → wedge).
- **Confirmed:** fresh-boot `date` = 1970; controlled — `date -s 2026` first →
  `fopen` succeeds (reaches polling). Only variable = clock.
- **Mechanism:** the file's pre-1980 mtime breaks the kernel FAT driver on the SD
  card. (Production avoids it via `ProcessLifecycle::syncSystemTime`; `snap_test`
  is standalone.)
- **Fix:** `snap_test::ensurePlausibleClock()` — if the system clock is <2026,
  `settimeofday` to 2026-06-22 before snapping (`src/app/snap_test.cpp`).

### #2 — target > sensor-native → framesource corruption → `IMP_Encoder_PollingStream` segfault
- **Symptom:** with a good clock, `snap_test snap 3840 2160` → `fopen` OK →
  `stream_->polling()` → user-space `Segmentation fault` inside
  `IMP_Encoder_PollingStream`.
- **Confirmed:** `snap_test snap 2560 1440` (sensor-native) → OK; `3840 2160`
  (> sensor 2560×1440) → segfault. Same boot position, same clock; only variable
  = target vs sensor. And `sample-Encoder-jpeg` (SDK JPEG sample, sensor-native)
  → RC=0 OK as 1st IMP process — so the ISP→encoder→JPEG flow is fundamentally
  fine; `snap_test`'s CH0 config was the difference.
- **Root cause:** `ImageSnap::initialize()` configured the CH0 framesource at the
  **target** resolution; for target > sensor-native this corrupts the sensor
  channel (segfault in `SetChnAttr`, then `IMP_Encoder_PollingStream`). The code
  comment at the same site already warned of this — but the sensor-native
  fallback was only applied for >8M, not for ≤8M up-scale (e.g. 3840 > 2560).
  Default `stillSize`=4M (=sensor) is why production never hit it.
- **Fix:** route **any** target > sensor-native to sensor-native capture +
  software upscale (the `LargeImageSnap` path) — not just >8M. `isLargeImage_`
  is now `target > sensorNative` (default sensor 2560×1440, gc4653). Targets
  ≤ sensor-native keep the HW encoder path (down/native). (`src/media/snap/
  ImageSnap.cpp::initialize`.) Note: this means ≤8M **up-scale** above sensor
  uses software scale (the JPEG IVDC path can't up-scale above sensor) — a HW
  limitation, not a regression.

### Validation (fresh cold boot, no manual clock, 1st IMP process)
- `snap_test snap 3840 2160 85 1` → auto-set clock + software-scale →
  `result=OK large=1 thumb=saved 813491B 2894ms` ✅
- `snap_test snap 2560 1440 85 1` → HW native →
  `result=OK large=0 thumb=saved 71603B 1062ms` ✅ (2nd IMP process, clean)
- B1 pytest `test_snap_smoke.py -k 4m-single` → **passed** (3rd IMP process,
  clean): the full harness (snap_test → result anchor → verdict) works end-to-end
  with no manual clock / no priming.

## Still open
- **Cross-process IMP wedge** remains (1-boot-1-case for the full B1 matrix;
  `snap_test` is fine as the 1st process per boot).
- **≤sensor burst** (e.g. `snap 2560 1440 85 3`, the `snap_internal` loop) is
  untested post-fix. Note the earlier `8m-burst(3840×3)` wedge no longer applies —
  3840 is now >sensor, so it routes to `snap_large_burst_internal` (temp-buffer
  flow), not `snap_internal`. The `snap_internal` burst loop is now only reached
  for ≤sensor bursts; re-validate when convenient.
- Carry the unified path into `quick_snap` (set `sensorNativeSize` + persist
  thumbnail) to close the production gaps.
