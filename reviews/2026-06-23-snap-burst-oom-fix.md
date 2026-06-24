# 2026-06-23 — snap 8m-burst OOM fix + full scenario-test-manifest HW run

Follow-up to [2026-06-22-snap-unification.md](2026-06-22-snap-unification.md).
The 2026-06-22 F3 burst path (capture N NV12 to SD, then load each full frame
into RAM and strip-encode) was memory-marginal on the 32MB board: it OOM-killed
on one fresh boot and passed on another (flaky). This note records the root cause,
the fix, and the full manifest execution that surfaced it.

## Full manifest run (Boots 1–12, all 1-boot-1-case for IMP)

Every manifest pattern exercised on real T32 — **all GREEN** (or honestly skip):

| layer | pattern | result |
|---|---|---|
| sim | verdict contract (23) · B5 ntp · B6 http_api | ✅ |
| HW no-IMP | net_smoke · wm2-rtc0/1 (upload cascade) | ✅ |
| HW snap | 4m-single · 8m-single · 16m-single · **8m-burst (fixed)** | ✅✅✅✅ |
| HW record | record_smoke · record_repeat (3-seg) | ✅✅ |
| HW um | um_mobile(wm3) · um_rtsp(wm4) | ✅✅ |
| HW wm matrix | wm0-rtc0 · wm1-rtc0 | ✅✅ |
| — | wm_repeat | ⏭️ permanent skip (cross-process IMP wedge = HW limit) |

Skipped as redundant (rtc is a no-op for the record/upload anchors): wm0-rtc1,
wm1-rtc1, snap 4m-nothumb (a thumb-off variant of the already-green 4m path).

## The 8m-burst OOM

- **Symptom:** `snap_test snap 3840 2160 85 3` (Boot 4) → `[83.88] Out of memory:
  Kill process (snap_test) score 89` → rc=137. `total-vm:148492kB, anon-rss:0kB`
  (anon pages swapped out → zram exhausted). 83s of swap-thrash before the kill.
- **But flaky:** the SAME code passed on Boot 5 (13.1s, 3 JPEGs). Same fresh-boot
  procedure → one boot OOMs, the next passes. Not production-acceptable.

### Root cause (pinned with `[mem]` instrumentation)
Added `/proc/meminfo` sampling at burst phase boundaries (printf→stdout, over
serial). Boot 5 profile (the passing boot):

| checkpoint | MemAvailable |
|---|---|
| burst:enter | 9904 |
| after-EnableChn | 9884 |
| capture-done (post-DisableChn) | 7824 |
| enc-frame-0-before | **2292** |
| enc-frame-1-before | 2272 |
| enc-frame-2-before | 2660 |

Two findings:
1. **No cross-frame accumulation** — frame 0/1/2 MemAvailable is flat (~2.3MB).
   `freeBuffers()` releases per-frame; `InputJpege` does not leak. The staging
   *design* (SD暂存 → 顺序编码) is memory-correct.
2. **The full-frame `nv12` staging vector is the marginal OOM trigger.** The drop
   capture-done→enc-frame-0 is exactly **−5.5MB** = the `std::vector<uint8_t>
   nv12(nv12Size)` (2560×1440×1.5) that Phase 2 loaded each frame into. With the
   IMP pool (~22MB) + OS already consuming most of 32MB, the burst peak
   (nv12 5.5MB + strip buffers ~3MB ≈ 8MB) left only **~1.7MB margin** → boot-to-
   boot memory variance tips it into OOM.

`DisableChn` does NOT free the framesource pool (capture-done only ~2MB below
after-EnableChn), but that is irrelevant — the encode survives on reclaimable
cache (MemAvailable >> MemFree); the nv12 vector is what eats the headroom.

### Fix — `LargeImageSnap::snapLargeFromFile` (strip-by-strip from the temp file)
Don't load the full NV12 frame into RAM. Read each strip's source rows directly
from the staged temp file (`fseek`+`fread` of the contiguous Y block then UV
block — 2 seeks/reads per strip, fast on SD). Only one strip's source rows
(~0.6MB) are in RAM at a time.

Refactored to keep the JPEG DRI/RST stitching logic in ONE place:
- `encodeLargeJpeg(out, dst, q, src_w, src_h, fillCropBuf)` — the shared strip-
  encode+stitch pipeline (geometry, buffers, header, loop, cleanup).
- `snapLargeFromBuffer(...)` — thin wrapper, `fillCropBuf` = `cropStrip` (memcpy
  from a RAM buffer; the single/large path — **unchanged behavior**).
- `snapLargeFromFile(...)` — thin wrapper, `fillCropBuf` = `cropStripFromFile`
  (fread from the temp file; the burst path).
- `ImageSnap::snap_large_burst_internal` Phase 2 now calls `snapLargeFromFile`
  with the temp path (no `nv12` vector, no full-frame `fread`).

### Verification (Boot 6, instrumented)

| checkpoint | before (nv12 vector) | after (snapLargeFromFile) |
|---|---|---|
| enc-frame-0 MemAvailable | 2292 | **7696 (+5404)** |
| enc-frame floor | ~2.3MB | **~7.7MB** |
| margin | ~1.7MB | **~7MB** |
| total (3 frames) | 13138ms | **8065ms (−40%)** |

3 JPEGs (821KB each), thumb=saved, result=OK. The +5.4MB at enc-frame is exactly
the eliminated nv12 vector — root cause confirmed, flakiness gone.

### Files
- `src/media/snap/LargeImageSnap.h` — `snapLargeFromFile` + private
  `encodeLargeJpeg`/`cropStripFromFile` decls; `+<cstdio><functional>`.
- `src/media/snap/LargeImageSnap.cpp` — `encodeLargeJpeg` (extracted loop) +
  `snapLargeFromBuffer`/`snapLargeFromFile` wrappers + `cropStripFromFile`.
- `src/media/snap/ImageSnap.cpp` — `snap_large_burst_internal` Phase 2 →
  `snapLargeFromFile` (nv12 vector + full-frame fread removed).

Dual-platform clean (sim + T32/uClibc). No `src/hal/**` touched. No commit (git
policy). Instrumentation (`logMemFree` printf) was added to pin the cause and
removed after the fix verified.

## Process lessons (saved to memory)
- **`snap_test` is a thin loader** — the snap logic lives in `libmedia_snap.so`
  (linked at runtime via `LD_LIBRARY_PATH`). `verify_deploy.sh snap_test` shows
  an unchanged executable md5 even after a snap-code change (only the .so md5
  moves). Always check the **.so** md5 for snap changes, not the executable.
- **`snap_test` survives dirty boots** (passed at 81min uptime / 3.9MB free);
  the full `htc_workmode_app` `initVideo` is the dirty-boot-sensitive path
  (`Unhandled kernel unaligned access`). So snap cases are not fussy about a
  pristine boot; record/um/wm cases are.

## Still open
- Carry the unified path + `sensorNativeSize` into `quick_snap`/`takePhoto`
  (production gaps; see 2026-06-22 review "Out of scope").
- `snap_internal` ≤sensor burst loop (only reached for ≤sensor multi-shot) is
  still untested post-fix (low priority — single ≤sensor is proven).
