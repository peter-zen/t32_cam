"""{mode,rtc} matrix (manifest §B7): run `htc_workmode_app -wm <mode> -rtc <rtc>`
for the ONE-SHOT modes and assert each hits its MODE-SPECIFIC success anchor
(upgrade from the Phase-1 crash/hang-only detector).

Mode-specific anchors (captured from real device runs 2026-06-22 — NOT guessed;
see manifest §6 Phase-0 lesson):
  - wm 0 (SNAP_ONLY → EventLoop record loop): `record start:` + `thumbnail saved`
    + `observed_fps` (same contract test_record_smoke pins). Needs
    HTC_SIM_PIR_INTERVAL_MS=999999 so only the entry-trigger record runs, then
    the loop sees upload-idle and shuts down (single segment).
  - wm 1 (SNAP_UPLOAD): on THIS device cameraMode routes to record (video-only),
    so the same record anchors + the upload-cascade anchor `mgmtServerAddr:`.
    (On a snap-capable cameraMode the snap path is silent — processCmdSnap — so
     there is no positive snap anchor to require; the record+upload anchors are
     the reliable signal. See test_snap_smoke.py for the snap-path story.)
  - wm 2 (UPLOAD_ONLY): `mgmtServerAddr:` (cascade reached the upload phase).

devtest env has no registered mgmt backend → `auth failed` / `connect [...] failed`
are the expected clean-failure E/ lines (benign), same as test_upload_smoke.

Modes 3 (mobile) and 4 (rtsp-server) are LONG-RUNNING services — tested by
test_um_mobile_probe.py / test_um_rtsp_probe.py (probe + ctrl-c teardown), NOT here.

IMP / 1-boot-1-case: wm0 and wm1 touch IMP. Running >1 IMP process in one boot
wedges (cross-process IMP residue — see test_wm_repeat.py). So invoke this matrix
ONE CASE PER BOOT, e.g.:
    # after a fresh boot:
    pytest tests/host/test_wm_modes_matrix.py -k '0-1' -s --junit-xml=logs/wm0_rtc1.xml
Reboot between IMP-touching cases. wm2 is client-only (no IMP) and may repeat.
"""
import pytest

from scenario_verdict import judge, VerdictSpec
from wm_verdict import check_run as record_check_run

pytestmark = pytest.mark.hardware

ONE_SHOT_MODES = [0, 1, 2]
RTC_VALUES = [0, 1]

# devtest device's credentials are rejected by the real mgmt server; the upload
# is EXPECTED to fail at auth/connect. These are clean-failure signatures.
_BENIGN_UPLOAD = (
    r"auth failed",
    r"connect \[[^\]]*\] failed",
    r"ntp server is empty",      # NTP subsystem, same cascade (if server unset)
)


def _app_run(mode, rtc):
    base = (
        "HTC_TEST_NO_POWEROFF=1 "
        "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
        f"/mnt/huntcam/bin/htc_workmode_app -wm {mode} -rtc {rtc}"
    )
    if mode == 0:
        # EventLoop: push the PIR interval out so only record #1 runs, then the
        # loop shuts down on upload-idle (single segment, as test_record_smoke).
        return "HTC_SIM_PIR_INTERVAL_MS=999999 " + base
    return base


def _verdict(mode, rtc, output, rc, timed_out):
    """Return {"ok", "reasons"} with mode-specific success anchors."""
    if mode == 0:
        # Record contract (pinned by test_record_smoke / test_verdict.py).
        v = record_check_run(output, rc, timed_out, run_no=f"{mode}/{rtc}")
        return {"ok": v["ok"], "reasons": v["reasons"]}
    # wm1 / wm2: upload cascade must reach mgmtServerAddr; wm1 also records
    # (cameraMode video-only on this device). auth/connect failure is benign.
    anchors = (r"mgmtServerAddr:",)
    if mode == 1:
        anchors = (r"record start:", r"mgmtServerAddr:")
    return judge(output, rc, timed_out, VerdictSpec(
        name=f"wm-matrix/wm{mode}-rtc{rtc}",
        required_anchors=anchors,
        benign_errors=_BENIGN_UPLOAD,
        rc_ok=(0,),
        run_no=f"{mode}/{rtc}"))


@pytest.mark.parametrize("mode", ONE_SHOT_MODES)
@pytest.mark.parametrize("rtc", RTC_VALUES)
def test_wm_oneshot_hits_mode_anchor(mode, rtc, device):
    r = device.run(_app_run(mode, rtc), timeout=150)
    v = _verdict(mode, rtc, r["output"], r["rc"], r["timed_out"])
    tail = r["output"][-1500:]
    assert v["ok"], ("; ".join(v["reasons"])
                     + f"\n--- device output tail ---\n{tail}")
