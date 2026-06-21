"""Tracer bullet (Phase-0): run `htc_workmode_app -wm 0 -rtc 1` TWICE in the
same boot and assert the 2nd run does not hang (rc 137) from IMP residue.

This single test exercises the entire devtest chain end to end — build → NFS
deploy (with noac) → devctl/broker over serial → HTC_TEST_NO_POWEROFF (so the
app returns to the shell instead of powering off) → deterministic verdict — and
directly validates the wm/um "single-process repeatable" stabilization.

Run (broker up, device mounted at /mnt/huntcam):
    pytest tests/host/test_wm_repeat.py --junit-xml=logs/wm_repeat.xml -s
"""
import pytest

from wm_verdict import check_run

# SKIPPED by default: this runs TWO `htc_workmode_app -wm 0` processes in one
# boot (cross-process). The IMP driver does NOT support a 2nd IMP-using process
# in the same boot — the 2nd process's IMP_System_Init wedges the kernel (needs
# manual power-cycle). This is a HARDWARE/SDK limitation, not fixable in app
# code (verified 2026-06-21: wedges whether or not process 1 calls
# IMP_System_Exit). To re-verify the limitation, remove this skip — expect a
# device wedge + cold-boot afterward.
pytestmark = [
    pytest.mark.hardware,
    pytest.mark.skip(reason="cross-process IMP wedge — hardware limitation; "
                            "2nd IMP process in one boot wedges the kernel"),
]

APP_RUN = (
    "HTC_TEST_NO_POWEROFF=1 "
    "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
    "/mnt/huntcam/bin/htc_workmode_app -wm 0 -rtc 1"
)


def test_wm0_repeat_twice_no_hang(device):
    for run_no in (1, 2):
        r = device.run(APP_RUN, timeout=120)
        verdict = check_run(r["output"], r["rc"], r["timed_out"], run_no)
        tail = r["output"][-1500:]
        assert verdict["ok"], (
            "; ".join(verdict["reasons"]) + f"\n--- device output tail ---\n{tail}"
        )
