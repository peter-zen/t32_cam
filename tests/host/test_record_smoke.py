"""Record smoke (Phase-1): a SINGLE `-wm 0` record run on a clean boot.

Distinct from the Phase-0 tracer (test_wm_repeat.py runs it TWICE to repro the
deterministic 2nd-segment IMP_Encoder_CreateChn crash). This single-shot variant
is the stabilization success bar for one record segment: record starts, produces
frames at a sane fps, exits 0, no non-benign error.

On a clean boot the 1st segment should pass (the crash is on the 2nd). If it
fails even single-shot, that's a new finding — surface it.

Run (broker up, clean boot):
    pytest tests/host/test_record_smoke.py -s --junit-xml=logs/record_smoke.xml
"""
import pytest

from wm_verdict import check_run

pytestmark = pytest.mark.hardware

APP_RUN = (
    # HTC_TEST_NO_POWEROFF: _exit(0) back to shell instead of poweroff (same-boot reruns).
    # The shutdown kill-switches (HTC_SKIP_ISP_DAYNIGHT_ON_SHUTDOWN /
    # HTC_SKIP_TEARDOWN_ON_SHUTDOWN) have been RETIRED — cleanupHook no longer calls
    # controlISP (was the defog ISR panic source), and RecordTask::stop always runs
    # releaseVideoResources (now channel-level via the process-static IngenicVideo,
    # no IMP_System_Exit). So no env kill-switches needed; this tests clean content.
    "HTC_TEST_NO_POWEROFF=1 "
    # -wm 0 is the EventLoop; push the PIR interval out so only the entry-trigger
    # record #1 runs, then the loop sees upload-idle and shuts down (single segment).
    "HTC_SIM_PIR_INTERVAL_MS=999999 "
    "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
    "/mnt/huntcam/bin/htc_workmode_app -wm 0 -rtc 1"
)


def test_wm0_single_record(device):
    r = device.run(APP_RUN, timeout=120)
    v = check_run(r["output"], r["rc"], r["timed_out"], run_no=1)
    tail = r["output"][-1500:]
    assert v["ok"], "; ".join(v["reasons"]) + f"\n--- device output tail ---\n{tail}"
