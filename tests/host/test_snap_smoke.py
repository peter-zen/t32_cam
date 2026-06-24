"""Photo + thumbnail snap smoke (manifest §B1, module 1): drives the unified
`ImageSnap::snap()` path via the `snap_test snap ...` harness across the spec —
variable main resolution, optional thumbnail, and burst.

The snap capture component is `ImageSnap` (NOT the pin-driven `quick_snap`/
`htc_media_app` orchestration); `snap_test` exercises it directly. The unified
path (src/media/snap) routes by target vs sensor-native (2560x1440):
  - target <= sensor-native -> snap_internal (CH0 HW encoder, down/native)
  - target >  sensor-native -> sensor-native capture + software upscale
    (LargeImageSnap strip; the JPEG IVDC path can't be configured above sensor —
     it corrupts the channel + segfaults in IMP_Encoder_PollingStream; the
     full-frame SIMD path is also dead on the 32MB board: a 48M NV12+JPEG pair
     is ~143MB). Single = strip in one go; burst (count>1) = temp-buffer flow
     (capture N NV12 to sdcard, then sequential strip-scale+encode).
Thumbnail is optional (ImageSnapParams.setThumbnailEnabled); CH2 is opened only
when enabled.

`snap_test` is self-sufficient on a fresh cold boot: it auto-sets a plausible
system clock (`ensurePlausibleClock`, <2026 -> 2026) — the fresh-boot RTC is 1970
and a pre-1980 file mtime triggers a kernel FAT oops on the SD card. So no manual
clock or priming is needed.

`snap_test snap` prints a machine-parseable result anchor on stdout (snap_test
uses std::cout, captured by the broker over serial — not EasyLogger):
  [snap_test] result=OK mode=snap size=3840x2160 large=1 count=1 thumb=saved \
              files=1 minbytes=<> maxbytes=<> total=<>ms
The verdict regex-parses it: result=OK, large matches the size class, count==
files, per-file bytes>0, thumb matches the on/off flag.

IMP / 1-boot-1-case: each snap_test run is one IMP process. Cross-process IMP
can wedge (cf. test_wm_repeat), so validate each case on its own cold boot
(`pytest -k '<case-id>'`); snap_test fully releases IMP on exit but a second IMP
process in the same boot is not guaranteed clean.

Run (broker up, fresh boot per case):
    pytest tests/host/test_snap_smoke.py -k 4m-single -s --junit-xml=logs/snap_4m.xml
"""
import re

import pytest

from scenario_verdict import strip_ansi

pytestmark = pytest.mark.hardware

_SNAP_TEST = "/mnt/huntcam/bin/snap_test"
_OUT_DIR = "/mnt/sdcard/media"

# (id, W, H, count, thumb_on, expect_large). Routing is target vs sensor-native
# (2560x1440): <= sensor -> HW encoder (large=0); > sensor -> software upscale
# (large=1). Burst > sensor uses the temp-buffer flow.
_CASES = [
    ("4m-single",   2560, 1440, 1, True,  False),  # <= sensor: HW native
    ("8m-single",   3840, 2160, 1, True,  True),   # > sensor: software upscale
    ("16m-single",  5120, 2880, 1, True,  True),   # > sensor: strip upscale
    ("8m-burst",    3840, 2160, 3, True,  True),   # > sensor burst: temp-buffer flow
    ("4m-nothumb",  2560, 1440, 1, False, False),  # <= sensor, thumbnail off
]

_ANCHOR = re.compile(
    r"\[snap_test\] result=(\w+) mode=snap size=(\d+)x(\d+) "
    r"large=(\d) count=(\d+) thumb=(\w+) files=(\d+) "
    r"minbytes=(-?\d+) maxbytes=(-?\d+)"
)


@pytest.mark.parametrize("cid,W,H,count,thumb,large", _CASES, ids=[c[0] for c in _CASES])
def test_snap_spec(device, cid, W, H, count, thumb, large):
    thumb_arg = "" if thumb else "--no-thumb "
    cmd = (
        f"LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
        f"{_SNAP_TEST} snap {W} {H} 85 {count} {thumb_arg}{_OUT_DIR}"
    )
    r = device.run(cmd, timeout=150)
    out = strip_ansi(r["output"])
    m = _ANCHOR.search(out)

    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']} (snap_test exited non-zero)")
    if not m:
        reasons.append("no result anchor in output")
    else:
        result, _, _, alarge, acount, athumb, afiles, amin, _amax = m.groups()
        if result != "OK":
            reasons.append(f"result={result}")
        if int(alarge) != (1 if large else 0):
            reasons.append(f"large={alarge} expected {1 if large else 0}")
        if int(acount) != count:
            reasons.append(f"count={acount} expected {count}")
        if int(afiles) != count:
            reasons.append(f"files={afiles} expected {count}")
        if int(amin) <= 0:
            reasons.append(f"minbytes={amin} (no JPEG data)")
        if thumb and athumb != "saved":
            reasons.append(f"thumb={athumb} expected saved (thumbnail capture failed)")
        if not thumb and athumb != "off":
            reasons.append(f"thumb={athumb} expected off (--no-thumb ignored)")

    assert not reasons, ("; ".join(reasons) + f"\n--- snap_test output tail ---\n{out[-1500:]}")
