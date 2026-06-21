"""Multi-segment record (Gap-2): force ONE process to record N segments via the
EventLoop HTC_TEST_RECORD_COUNT test hook, and assert record#2/#3 succeed — no
IMP_Encoder_CreateChn (the channel-not-released defect task-9 targets).

The EventLoop normally shuts down after record#1 (upload-idle — no backend in the
devtest env). HTC_TEST_RECORD_COUNT=N gates that shutdown until N segments
complete; HTC_SIM_PIR_INTERVAL_MS=5000 makes the SimPirTrigger re-fire soon after
each segment finishes so #2/#3 actually get triggered. This is the Q3
"可重复触发" (in-process repeatable recording) verification.

Run on a CLEAN COLD BOOT (one IMP-using process per boot — cross-process repeat
is a hardware limitation, see test_wm_repeat.py):
    pytest tests/host/test_record_repeat.py -s --junit-xml=logs/record_repeat.xml
"""
import pytest

from wm_verdict import check_repeat_run

pytestmark = pytest.mark.hardware

SEGMENTS = 3

APP_RUN = (
    "HTC_TEST_NO_POWEROFF=1 "
    f"HTC_TEST_RECORD_COUNT={SEGMENTS} "   # gate shutdown until N segments complete
    "HTC_SIM_PIR_INTERVAL_MS=5000 "        # re-trigger soon after each segment ends
    "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
    "/mnt/huntcam/bin/htc_workmode_app -wm 0 -rtc 1"
)


def test_wm0_records_three_segments(device):
    r = device.run(APP_RUN, timeout=200)
    v = check_repeat_run(r["output"], r["rc"], r["timed_out"],
                         expected_count=SEGMENTS, run_no=1)
    tail = r["output"][-1800:]
    assert v["ok"], ("; ".join(v["reasons"])
                     + f"\n--- device output tail ---\n{tail}")
