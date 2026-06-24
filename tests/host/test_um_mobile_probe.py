"""Mobile usermode probe (manifest §B3, modules 8+7+5): the wm3 long-running
service (mDNS + HTTP + RTSP) comes up, and SIGINT tears it down cleanly.

`-wm 3` = TEST_ONLY → CMD_MOBILE (workModeToCommand :878). Unlike the one-shot
modes, CMD_MOBILE enters a `while (keepRunning())` loop and does NOT exit on its
own, so the test backgrounds it, proves the services came up, then SIGINTs for
teardown (the real stabilization test for long-running um).

Verdict (all source-verified, 2026-06-22):
  - HARD: HTTP port 80 listening in /proc/net/tcp (CivetWeb bound it).
  - HARD: this run's startup anchors in /mnt/sdcard/logs/app.log (the app's
    EasyLogger file sink — the broker's serial capture is unreliable across
    boots, but app.log is not). The app TRUNCATES app.log on startup, so the
    test truncates it first too — guaranteeing any anchor found is from THIS run.
    Anchors: 'HTTP server started on port 80', 'RTSP server started on port 8554'.
  - HARD: device shell still responsive after SIGINT teardown (not wedged).

Two device realities this test accounts for:
  1. busybox has no nohup/head/tail/sed/wc AND `grep` on /proc/<pid>/cmdline
     stops at the first NUL — so the cleanup matches the binary basename
     `workmode_app` (before the NUL), not `-wm 3` (after it). A lingering wm3
     holds port 80 and makes the next instance's http_server_start fail with
     'Failed to start HTTP server'; the cleanup prevents that.
  2. this T32 is 32MB RAM / ~2MB free + 16MB zram swap — wm3 (mDNS+HTTP+RTSP+
     video) swap-thrashes its init, taking ~38s spawn→anchors (vs <1s on a
     memory-healthy boot). _WARMUP_S=45s gives margin; teardown is likewise slow.

WiFi/DHCP skip on the already-associated device (Misc.cpp:432) — bringing wm3 up
does NOT disturb the NFS-over-WiFi link. HTC_TEST_NO_POWEROFF=1 is mandatory so
the teardown _exit(0)s instead of powering the board off. 1-boot-1-case (wm3
touches IMP via the RTSP video pipeline); run on a fresh boot.

Run (broker up, fresh boot):
    pytest tests/host/test_um_mobile_probe.py -s --junit-xml=logs/um_mobile.xml
"""
import re
import time

import pytest

pytestmark = pytest.mark.hardware

_LOG = "/mnt/sdcard/logs/app.log"

_SPAWN = (
    "HTC_TEST_NO_POWEROFF=1 "
    "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
    "/mnt/huntcam/bin/htc_workmode_app -wm 3 -rtc 1 >/dev/null 2>&1 & "
    "echo PID=$!"
)
# Kill any lingering workmode_app (hold port 80 → next http_server_start fails).
# Match the basename (before /proc/<pid>/cmdline's first NUL — busybox grep
# stops there, so "-wm 3" after the NUL never matches).
_CLEANUP = (
    "for d in /proc/[0-9]*; do "
    "grep -qa workmode_app \"$d/cmdline\" 2>/dev/null && kill -9 ${d##*/} 2>/dev/null; "
    "done; sleep 2; echo OK"
)
_WARMUP_S = 45    # swap-thrashed init on the 32MB board (~38s observed)
_TEARDOWN_S = 12  # SIGINT teardown is slow under swap too


def _pid_of(spawn_output):
    m = re.search(r"PID=(\d+)", spawn_output)
    return m.group(1) if m else None


def _anchor_count(device, anchor):
    r = device.run(f"N=$(grep -ac '{anchor}' {_LOG} 2>/dev/null); echo CNT=$N", timeout=10)
    m = re.search(r"CNT=(\d+)", r["output"])
    return int(m.group(1)) if m else -1


def test_wm3_mobile_probe_and_clean_teardown(device):
    # 0) Clean baseline: kill lingering wm3 (free port 80), then truncate app.log
    #    so any anchor found later is provably from THIS run.
    device.run(_CLEANUP, timeout=20)
    device.run(f"> {_LOG}", timeout=8)

    # 1) Background-spawn wm3.
    sp = device.run(_SPAWN, timeout=15)
    pid = _pid_of(sp["output"])
    assert pid, f"no PID captured from spawn output: {sp['output']!r}"

    # 2) Warm up (slow under swap), then verify services bound + logged.
    time.sleep(_WARMUP_S)
    port80 = device.run("grep -i :0050 /proc/net/tcp", timeout=10)   # HTTP 80 LISTEN
    http_n = _anchor_count(device, r"HTTP server started on port 80")
    rtsp_n = _anchor_count(device, r"RTSP server started on port 8554")

    reasons = []
    if port80["rc"] != 0:
        reasons.append("HTTP port 80 not listening (/proc/net/tcp)")
    if http_n < 1:
        reasons.append("no 'HTTP server started on port 80' in app.log this run")
    if rtsp_n < 1:
        reasons.append("no 'RTSP server started on port 8554' in app.log this run")

    # 3) SIGINT teardown, confirm the process exited (slow under swap).
    device.run(f"kill -INT {pid}", timeout=10)
    time.sleep(_TEARDOWN_S)
    alive = device.run(f"kill -0 {pid} 2>/dev/null; echo RC=$?", timeout=10)
    if "RC=0" in alive["output"]:
        reasons.append(f"process {pid} still alive {_TEARDOWN_S}s after SIGINT (hang)")

    # 4) Shell still responsive (not wedged).
    probe = device.run("echo UP", timeout=10)
    if "UP" not in probe["output"]:
        reasons.append("device shell unresponsive after teardown (wedge)")

    assert not reasons, "; ".join(reasons)
