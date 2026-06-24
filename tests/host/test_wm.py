"""wm app HW verification matrix (wm-app-spec). Drives the new `wm` binary
(`src/app/wm_app.cpp`) across mode × cameraMode, 1-boot-1-case.

**1-boot-1-case (HARD)**: IMP 1-wm-per-boot — a 2nd IMP-using `wm` (m0/m1) in the
same boot wedges the kernel (manual power-cycle to recover). So run ONE case per
cold boot, never the whole file in one boot:
    pytest tests/host/test_wm.py -k m0-photo -s        # then cold-boot, next case
(m2 is IMP-free — safe to run any time, including after an m0/m1 case in the same boot.)

Each case: `wm -m <mode>` with HTC_WM_CAMERA_MODE / ONE_SHOT / SimPir / idle-grace
knobs, then assert on **app.log** (`/mnt/sdcard/logs/app.log`) — the serial-console
capture drops lines across runs; app.log is truncated per app-startup so it holds
exactly this run. Verdict: `[wm] op=boot/start/shutdown` all present, the expected
capture lane ran (SnapTask photo / RecordTask record / both for cm==1), and no crash
signature (panic/Segment/defog — the ISP ISR defog panic that killed the old app).

devtest has no mgmt/storage backend → upload always auth-fails (expected, benign);
this asserts the capture+shutdown path, not upload success (cf. manifest B2).

Run (broker up, fresh boot per IMP case):
    pytest tests/host/test_wm.py -k m0-photo -s --junit-xml=logs/wm_m0_photo.xml
"""
import pytest

from scenario_verdict import strip_ansi

pytestmark = pytest.mark.hardware

_WM = "/mnt/huntcam/bin/wm"
_LIBS = "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH"
_APPLOG = "/mnt/sdcard/logs/app.log"

# (id, mode, cameraMode, expect_kind). expect_kind: "photo"/"record"/"both"/None.
# m2 has no capture (cameraMode irrelevant). cm==3 (concurrent) out of wm scope.
_CASES = [
    ("m0-photo",   0, 0,    "photo"),
    ("m0-record",  0, 2,    "record"),
    ("m0-both",    0, 1,    "both"),    # cm==1: photo then record (sequential)
    ("m1-photo",   1, 0,    "photo"),
    ("m1-record",  1, 2,    "record"),
    ("m1-both",    1, 1,    "both"),
    ("m2-upload",  2, None, None),       # IMP-free; upload-only drain
]

# Crash signatures that must NEVER appear (the ISP ISR defog panic killed the old app).
_CRASH = ("panic", "segment", "defog", "Unhandled kernel")

# cm==1 (photo+record combo) WEDGES the device (kernel hang — confirmed 2026-06-24:
# ImageSnap photo then CameraRecorder record in one trigger wedges IMP). Single-
# capture paths (cm==0 photo, cm==2 record) are GREEN. cm==1 combo deferred — needs
# IMP channel-management investigation. Marked xfail so the matrix documents it.
_XFAIL_CM1 = {"m0-both", "m1-both"}


def _run_wm(device, mode, camera_mode):
    env = (
        "HTC_TEST_NO_POWEROFF=1 HTC_WM_ONE_SHOT=1 "
        "HTC_SIM_PIR_INTERVAL_MS=5000 HTC_WM_IDLE_GRACE_MS=8000 "
        "HTC_UPLOAD_TIMEOUT_MS=15000"
    )
    if camera_mode is not None:
        env += f" HTC_WM_CAMERA_MODE={camera_mode}"
    cmd = f"{_LIBS} {env} {_WM} -m {mode}"
    return device.run(cmd, timeout=90)


def _applog(device):
    # app.log is truncated on wm startup → holds exactly this run. Read the
    # anchor + capture + crash lines in one grep.
    pat = r"op=boot|op=start|op=shutdown|SnapTask:|RecordTask:|panic|Segment|defog|Unhandled kernel"
    r = device.run(f"grep -E '{pat}' {_APPLOG} 2>/dev/null", timeout=20)
    return strip_ansi(r["output"])


@pytest.mark.parametrize("cid,mode,cm,kind", _CASES, ids=[c[0] for c in _CASES])
def test_wm_mode(device, cid, mode, cm, kind):
    if cid in _XFAIL_CM1:
        pytest.xfail("cm==1 photo+record combo wedges IMP (2026-06-24); single-capture "
                     "paths (cm==0/2) GREEN. Deferred — needs IMP channel-mgmt investigation.")
    _run_wm(device, mode, cm)
    log = _applog(device)
    low = log.lower()

    reasons = []
    if "op=boot" not in log:
        reasons.append("no op=boot anchor")
    if "op=start" not in log:
        reasons.append("no op=start anchor")
    if "op=shutdown" not in log:
        reasons.append("no op=shutdown (wm hung — IMP residue, or wedged)")
    if kind in ("photo", "both") and "SnapTask:" not in log:
        reasons.append("expected SnapTask (photo) log, none")
    if kind in ("record", "both") and "RecordTask:" not in log:
        reasons.append("expected RecordTask (record) log, none")
    for bad in _CRASH:
        if bad in low:
            reasons.append(f"crash signature: {bad}")

    assert not reasons, ("; ".join(reasons) + f"\n--- app.log tail ---\n{log[-1800:]}")
