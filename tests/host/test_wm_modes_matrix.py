"""{mode,rtc} matrix (Phase-1): run `htc_workmode_app -wm <mode> -rtc <rtc>` for
the ONE-SHOT modes and assert each exits clean — rc 0, no hang, no watchdog.

Conservative verdict ON PURPOSE. The Phase-0 mistake was verdict anchors that
didn't match the app's real log format (it prints "record start:", not the
assumed "record started: file="). So this matrix asserts only what is
unconditionally true of a healthy one-shot run: it did not crash (rc 0), did not
hang (not timed out), was not watchdog-killed (not 137). The devtest env has no
mgmt/NTP/upload backend, so those E/ lines are treated as benign noise — this is
a CRASH/HANG detector, not a feature-completeness check. Mode-specific success
anchors get added once captured on HW per mode.

Modes 3 (mobile: mdns+http+rtsp) and 4 (rtsp-server) are LONG-RUNNING services
(a while-keepRunning loop) — they need a probe-then-ctrl-c teardown test, not a
"exits clean" one. Not implemented here; tracked in the /devtest skill TODO.

IMPORTANT — IMP residue: every -wm run touches IMP. Running the whole matrix in
one boot accumulates residue and wedges (the deterministic IMP_Encoder_CreateChn
defect on the 2nd record + the dirty-boot initVideo kernel unaligned access).
This matrix is CODE-READY; end-to-end execution is gated on the wm/um
stabilization landing clean single-process teardown. Until then: one case per
cold boot.
"""
import pytest

from scenario_verdict import judge, VerdictSpec

pytestmark = pytest.mark.hardware

# One-shot modes only. 3/4 are long-running services (see module docstring).
ONE_SHOT_MODES = [0, 1, 2]
RTC_VALUES = [0, 1]

# devtest env has no mgmt/NTP/upload backend and the link is already up, so every
# E/ line those subsystems emit is expected noise → don't fail the crash/hang
# verdict on them. (r".*" benigns every E/ line; the rc/timeout checks still fire.)
_BENIGN_ALL = (r".*",)


@pytest.mark.parametrize("mode", ONE_SHOT_MODES)
@pytest.mark.parametrize("rtc", RTC_VALUES)
def test_wm_oneshot_exits_clean(mode, rtc, device):
    r = device.run(
        f"HTC_TEST_NO_POWEROFF=1 "
        f"LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
        f"/mnt/huntcam/bin/htc_workmode_app -wm {mode} -rtc {rtc}",
        timeout=120)
    v = judge(r["output"], r["rc"], r["timed_out"], VerdictSpec(
        name=f"wm-matrix/wm{mode}-rtc{rtc}",
        benign_errors=_BENIGN_ALL,
        rc_ok=(0,),
        run_no=f"{mode}/{rtc}"))
    assert v["ok"], ("; ".join(v["reasons"])
                     + f"\n--- device output tail ---\n{r['output'][-1200:]}")
