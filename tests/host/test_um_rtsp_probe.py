"""RTSP-server usermode probe (manifest §B4, module 5): the wm4 long-running
RTSP server comes up, and SIGINT tears it down cleanly.

`-wm 4` = UVC → CMD_CONN_NET | CMD_DHCP | CMD_RTSP_SERVER (workModeToCommand
:889). Like wm3 it is a `while (keepRunning())` loop (runRtspServerUntilSignal)
and does not exit on its own; the test backgrounds it, proves RTSP came up, then
SIGINTs for teardown — the clean-teardown contract for the RTSP video pipeline.

Verdict + device realities mirror test_um_mobile_probe.py: app.log is the reliable
capture (truncated first so any anchor is from THIS run); cleanup matches the
`workmode_app` basename (busybox grep stops at /proc/<pid>/cmdline's first NUL);
warmup/teardown are generous (32MB board swap-thrashes wm4's init). The RTSP
anchor 'RTSP server started on port 8554' (RtspServer.cpp:398) is HARD. The
/proc/net/tcp :2176 probe is SOFT — the RTSP listen socket does not reliably
appear there (wm3's RTSP logged 'started' without a :2176 entry), so the app.log
anchor is the trusted signal. WiFi/DHCP skip on the already-associated device
(Misc.cpp:432). HTC_TEST_NO_POWEROFF=1 mandatory. 1-boot-1-case (wm4 touches
IMP); run on a fresh boot.

Run (broker up, fresh boot):
    pytest tests/host/test_um_rtsp_probe.py -s --junit-xml=logs/um_rtsp.xml
"""
import re
import time

import pytest

pytestmark = pytest.mark.hardware

_LOG = "/mnt/sdcard/logs/app.log"

_SPAWN = (
    "HTC_TEST_NO_POWEROFF=1 "
    "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
    "/mnt/huntcam/bin/htc_workmode_app -wm 4 -rtc 1 >/dev/null 2>&1 & "
    "echo PID=$!"
)
_CLEANUP = (
    "for d in /proc/[0-9]*; do "
    "grep -qa workmode_app \"$d/cmdline\" 2>/dev/null && kill -9 ${d##*/} 2>/dev/null; "
    "done; sleep 2; echo OK"
)
_WARMUP_S = 45
_TEARDOWN_S = 12


def _pid_of(spawn_output):
    m = re.search(r"PID=(\d+)", spawn_output)
    return m.group(1) if m else None


def _anchor_count(device, anchor):
    r = device.run(f"N=$(grep -ac '{anchor}' {_LOG} 2>/dev/null); echo CNT=$N", timeout=10)
    m = re.search(r"CNT=(\d+)", r["output"])
    return int(m.group(1)) if m else -1


def test_wm4_rtsp_probe_and_clean_teardown(device):
    device.run(_CLEANUP, timeout=20)
    device.run(f"> {_LOG}", timeout=8)

    sp = device.run(_SPAWN, timeout=15)
    pid = _pid_of(sp["output"])
    assert pid, f"no PID captured from spawn output: {sp['output']!r}"

    time.sleep(_WARMUP_S)
    rtsp_n = _anchor_count(device, r"RTSP server started on port 8554")
    # Soft: RTSP listen socket doesn't reliably show in /proc/net/tcp.
    port = device.run("grep -i :2176 /proc/net/tcp", timeout=10)

    reasons = []
    if rtsp_n < 1:
        reasons.append("no 'RTSP server started on port 8554' in app.log this run")

    device.run(f"kill -INT {pid}", timeout=10)
    time.sleep(_TEARDOWN_S)
    alive = device.run(f"kill -0 {pid} 2>/dev/null; echo RC=$?", timeout=10)
    if "RC=0" in alive["output"]:
        reasons.append(f"process {pid} still alive {_TEARDOWN_S}s after SIGINT (hang)")

    probe = device.run("echo UP", timeout=10)
    if "UP" not in probe["output"]:
        reasons.append("device shell unresponsive after teardown (wedge)")

    assert not reasons, ("; ".join(reasons)
                         + f" (rtsp :2176 in /proc/net/tcp rc={port['rc']})")
