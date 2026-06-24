"""Upload smoke (manifest §B2, module 4): the upload cascade runs end-to-end
and fails CLEANLY in the devtest env — no crash, no hang.

`-wm 2` = UPLOAD_ONLY (workModeToCommand :872) → CMD_CONN_NET | CMD_DHCP |
CMD_NTP | CMD_UPLOAD. On the devtest device the link is already up, so CONN_NET
("WiFi already connected, skip") and DHCP ("wlan0 already has IP, skip") no-op;
NTP reaches the real server and syncs; the upload cascade prints
`mgmtServerAddr: <server>` (WorkModeRunner.cpp:714) then connects to the real
mgmt server, which REJECTS the devtest device's credentials → `auth failed`
(WorkModeRunner.cpp:727) → return Continue → clean shutdown.

So this scenario can only assert "upload cascade ran + failed cleanly + no
crash/no hang", NOT "upload succeeded" (the devtest device is not registered
with the mgmt server; a real upload needs a registered backend — Phase-2).
Required anchor `mgmtServerAddr:` proves the cascade reached the upload phase;
`auth failed` / `connect [...] failed` are the expected clean-failure E/ lines.

Real anchors captured from a 2026-06-22 device scout run (NOT guessed — see
manifest §6 Phase-0 lesson):
  mgmtServerAddr: www.aidetcloud.com
  connect [www.aidetcloud.com:8899] success   (then)
  E/LEGACY auth failed

wm2 is client-networking only (no camera/encoder/ISP) → does NOT touch IMP, so
it is safe to re-run without a cold boot (unlike wm0/3/4).

Run (broker up, link up):
    pytest tests/host/test_upload_smoke.py -s --junit-xml=logs/upload_smoke.xml
"""
import pytest

from scenario_verdict import judge, VerdictSpec

pytestmark = pytest.mark.hardware

APP_RUN = (
    # HTC_TEST_NO_POWEROFF: _exit(0) back to shell instead of poweroff.
    "HTC_TEST_NO_POWEROFF=1 "
    "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
    "/mnt/huntcam/bin/htc_workmode_app -wm 2 -rtc 1"
)

# The devtest device's credentials are rejected by the real mgmt server — the
# upload is EXPECTED to fail at auth (or connect, if the server is unreachable
# in some env). These are the clean-failure signatures, not defects.
_BENIGN = (
    r"auth failed",
    r"connect \[[^\]]*\] failed",
)


def test_wm2_upload_cascade_runs_and_fails_clean(device):
    r = device.run(APP_RUN, timeout=120)
    v = judge(r["output"], r["rc"], r["timed_out"], VerdictSpec(
        name="upload-smoke/wm2",
        required_anchors=(r"mgmtServerAddr:",),   # cascade reached the upload phase
        benign_errors=_BENIGN,
        rc_ok=(0,),
        run_no=1))
    tail = r["output"][-1500:]
    assert v["ok"], "; ".join(v["reasons"]) + f"\n--- device output tail ---\n{tail}"
