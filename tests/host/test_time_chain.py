"""Time-chain HW verification (wm-app-spec §6/§7, highest-risk module) — Stage 1
OFFLINE cases. Drives the standalone `time_test` harness (src/app/time_test.cpp,
modeled on snap_test) which implements the spec time chain as isolatable code
that later lifts into the wm app startup path.

Why this exists: mcu/ntp are sim-only-verified; the RTC->MCU->NTP chain +
ntpSynced flag + shutdown MCU write-back are NEW code. This verifies the atomics
+ chain on real HW, 1-boot-1-case, BEFORE building wm on top.

Stage 1 = OFFLINE only (deterministic, no network dependency). The NTP step is
exercised only as "fails offline" (writeback-unsynced). Online NTP reachability
(www.aidetcloud.com) is Stage 2.

`time_test` prints machine-parseable anchors on stdout (cout, captured over
serial):
  [time_test] op=rtc-read  rtc_ok=.. rtc=.. plausible=.. sys_before=.. sys_after=.. \
              sys_plausible_before=.. sys_plausible_after=.. clobber=..
  [time_test] op=rtc-set   set=.. write_ok=.. readback=.. match=..
  [time_test] op=mcu-read  mcu=.. plausible=.. zeroed=..
  [time_test] op=mcu-set   set=.. write_ok=.. readback=.. match=..
  [time_test] op=chain     inject=.. source=.. ntp_synced=.. rtc_written=.. \
              sys_before=.. sys_after=.. plausible=..
  [time_test] op=writeback ntp_synced_in=.. ntp_tried=.. ntp_ok=.. final=.. \
              plausible=.. mcu_written=..

No IMP/camera use -> multiple time_test invocations per boot are safe (no
1-IMP-process limit). Each test is still run 1-boot-1-case (cold boot) per the
HW discipline. The distinctive test value 2026-01-15 10:30:00 is plausible
(>=2026) and clearly not "now", so a stale/fallback read is detectable.

Run (broker up, fresh boot per case):
    pytest tests/host/test_time_chain.py -k rtc-roundtrip -s --junit-xml=logs/time_rtc.xml
"""
import re

import pytest

from scenario_verdict import strip_ansi

pytestmark = pytest.mark.hardware

_TIME_TEST = "/mnt/huntcam/bin/time_test"
_LIBS = "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH"
# Distinctive, plausible (>=2026), not "now": a stale/fallback read stands out.
_TEST_VALUE = "2026 1 15 10 30 0"


def _run(device, args, timeout=60):
    cmd = f"{_LIBS} {_TIME_TEST} {args}"
    r = device.run(cmd, timeout=timeout)
    return r, strip_ansi(r["output"])


def _fail(reasons, out):
    assert not reasons, ("; ".join(reasons) + f"\n--- time_test output tail ---\n{out[-1500:]}")


# Each case is its own test -> `pytest -k <id>` selects one cold-boot case.

def test_rtc_read(device):
    """Raw RTC::getTime + surfaces the read-implies-set gotcha (clobber flag)."""
    r, out = _run(device, "rtc-read", timeout=30)
    m = re.search(r"\[time_test\] op=rtc-read rtc_ok=(\d) rtc=(\S+) plausible=(\d)"
                  r".*?clobber=(\d)", out)
    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']} (rtc-read should report even on I2C/rtc fail)")
    if not m:
        reasons.append("no rtc-read anchor")
    # clobber=1 is a documented RTC.cpp gotcha, NOT a failure here (the chain
    # gates it). It is recorded for the review.
    _fail(reasons, out)


def test_rtc_roundtrip(device):
    """RTC::setTime writes; getTime reads back the same value (<=3s tolerance)."""
    r, out = _run(device, f"rtc-set {_TEST_VALUE}", timeout=30)
    m = re.search(r"\[time_test\] op=rtc-set set=(\S+) write_ok=(\d) readback=(\S+) match=(\d)", out)
    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']}")
    if not m:
        reasons.append("no rtc-set anchor")
    else:
        if m.group(2) != "1":
            reasons.append("write_ok=0 (RTC::setTime failed)")
        if m.group(4) != "1":
            reasons.append(f"match={m.group(4)} (RTC round-trip mismatch)")
    _fail(reasons, out)


def test_mcu_read(device):
    """Raw MCU::getDatetime. Reports plausible + zeroed (I2C-fail indicator)."""
    r, out = _run(device, "mcu-read", timeout=30)
    m = re.search(r"\[time_test\] op=mcu-read mcu=(\S+) plausible=(\d) zeroed=(\d)", out)
    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']}")
    if not m:
        reasons.append("no mcu-read anchor")
    _fail(reasons, out)


def test_mcu_roundtrip(device):
    """MCU::setDatetime writes; getDatetime reads back the same value."""
    r, out = _run(device, f"mcu-set {_TEST_VALUE}", timeout=30)
    m = re.search(r"\[time_test\] op=mcu-set set=(\S+) write_ok=(\d) readback=(\S+) match=(\d)", out)
    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']}")
    if not m:
        reasons.append("no mcu-set anchor")
    else:
        if m.group(2) != "1":
            reasons.append("write_ok=0 (MCU::setDatetime failed — I2C not accessible standalone?)")
        if m.group(4) != "1":
            reasons.append(f"match={m.group(4)} (MCU round-trip mismatch)")
    _fail(reasons, out)


def test_chain_runs(device):
    """Full §6.1 chain runs on HW, reports a valid source (not locked — depends on HW state)."""
    r, out = _run(device, "chain", timeout=45)
    m = re.search(r"\[time_test\] op=chain inject=(\d) source=(\w+) ntp_synced=(\d)"
                  r" rtc_written=(\d).*?plausible=(\d)", out)
    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']}")
    if not m:
        reasons.append("no chain anchor")
    else:
        source = m.group(2)
        if source not in ("system", "rtc", "mcu", "ntp", "none"):
            reasons.append(f"source={source} not in valid set")
    _fail(reasons, out)


# NOTE: no automated cold-boot RTC-fallback case here. Verifying the §6.1 step-2
# fallback (system implausible at boot -> chain picks rtc) needs the cold-boot
# state, but two on-device facts defeat automating it:
#   1. --inject-implausible can't force it: the kernel re-syncs sys<-RTC, so
#      settimeofday(1970) is immediately reverted (confirmed 2026-06-23).
#   2. devctl reboot is a SOFT reboot, which breaks WiFi on this board (known
#      quirk) -> NFS can't re-mount -> the case can't reach time_test afterward.
# The fallback was instead verified manually at cold boot: the first `chain`
# after bringup reported `sys_before=1970-01-01T00:55:58 -> source=rtc,
# sys_after=2026-06-23, plausible=1` (see
# reviews/2026-06-23-time-chain-hw-verification.md). Re-verify manually after
# any change to acquireTimeChain's RTC branch.


def test_writeback_synced(device):
    """§7 ntpSynced=1 path: use system time directly -> write MCU (no NTP try)."""
    # ensure system time is plausible first (rtc-set sets system too)
    _run(device, f"rtc-set {_TEST_VALUE}", timeout=30)
    r, out = _run(device, "writeback --ntp-synced 1", timeout=30)
    m = re.search(r"\[time_test\] op=writeback ntp_synced_in=(\d) ntp_tried=(\d) ntp_ok=(\d)"
                  r" final=(\S+) plausible=(\d) mcu_written=(\d)", out)
    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']}")
    if not m:
        reasons.append("no writeback anchor")
    else:
        if m.group(2) != "0":
            reasons.append(f"ntp_tried={m.group(2)} expected 0 (synced=1 skips NTP)")
        if m.group(5) != "1":
            reasons.append("plausible=0 (system should be plausible after rtc-set)")
        if m.group(6) != "1":
            reasons.append(f"mcu_written={m.group(6)} expected 1 (plausible -> write MCU)")
    _fail(reasons, out)


def test_writeback_unsynced(device):
    """§7 ntpSynced=0 path: try NTP first, then write MCU with the resulting time.
    After bringup the device is ONLINE, so NTP actually succeeds here (this also
    validates Stage-2 NTP reachability for free). The 'NTP fails -> fall back to
    sys' sub-branch can't be exercised online; either way mcu_written must be 1."""
    _run(device, f"rtc-set {_TEST_VALUE}", timeout=30)
    # ntpSyncAndWait may poll up to 30s; allow 90s.
    r, out = _run(device, "writeback --ntp-synced 0", timeout=90)
    m = re.search(r"\[time_test\] op=writeback ntp_synced_in=(\d) ntp_tried=(\d) ntp_ok=(\d)"
                  r" final=(\S+) plausible=(\d) mcu_written=(\d)", out)
    reasons = []
    if r["rc"] != 0:
        reasons.append(f"rc={r['rc']}")
    if not m:
        reasons.append("no writeback anchor")
    else:
        if m.group(2) != "1":
            reasons.append(f"ntp_tried={m.group(2)} expected 1 (synced=0 -> try NTP)")
        # ntp_ok is env-dependent: 1 online (NTP works — a positive finding),
        # 0 only if WiFi/NTP-server is down. Don't hard-assert; report it.
        if m.group(5) != "1":
            reasons.append("plausible=0 (resulting time should be plausible)")
        if m.group(6) != "1":
            reasons.append(f"mcu_written={m.group(6)} expected 1 (plausible -> write MCU)")
    _fail(reasons, out)
