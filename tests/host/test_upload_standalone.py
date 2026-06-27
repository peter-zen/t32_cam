"""Upload standalone (separate from wm cascade): runs `upload_test` directly.

This is the devtest pytest driver for the new tools/upload_test binary.
The binary exercises only the upload path (MgmtServClient + StorageServClient
via app_workmode::UploadWorker), without the full htc_workmode_app -wm 2
cascade (NTP/DHCP/MDNS/RTSP/HTTP server/HB). It can run on the devtest
device with the same broker/link-up preconditions as test_upload_smoke.py,
but skips the cascade noise.

Anchors:
  - `mgmtServerAddr:` printed by upload_test before UploadWorker::start
  - `desc_path:`       printed after the temp desc is written
  - `upload_test:`     final result line (flush_done / rc / elapsed_ms)

`auth failed` / `connect [...] failed` from UploadWorker::ensureConnected are
EXPECTED on the devtest device (credentials rejected by the real mgmt server,
mirroring test_upload_smoke.py's `_BENIGN` set).

Run (broker up, link up):
    pytest tests/host/test_upload_standalone.py -s --junit-xml=logs/upload_standalone.xml
"""
import pytest

from scenario_verdict import judge, VerdictSpec

pytestmark = pytest.mark.hardware

# Any pre-existing jpg in /mnt/huntcam/DCIM works. The binary reads its
# filename + parent dir to build a minimal desc; PID is pulled from
# DeviceConfig ([DEVICE] PID in /mnt/huntcam/config.ini).
_DEFAULT_JPG = "/mnt/huntcam/DCIM/IMG_20260119_112056.jpg"

APP_RUN = (
    "HTC_TEST_NO_POWEROFF=1 "
    "LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
    f"/mnt/huntcam/bin/upload_test {_DEFAULT_JPG} "
    "www.aidetcloud.com 8899 30"
)

# Same clean-failure signatures as test_upload_smoke.py: devtest creds rejected
# by the real mgmt server, so the cascade is EXPECTED to fail at auth/connect.
_BENIGN = (
    r"auth failed",
    r"connect \[[^\]]*\] failed",
)


def test_upload_standalone_cascade_runs_and_fails_clean(device):
    r = device.run(APP_RUN, timeout=60)
    v = judge(r["output"], r["rc"], r["timed_out"], VerdictSpec(
        name="upload-standalone/desc",
        required_anchors=(
            r"mgmtServerAddr:",   # binary reached the upload phase
            r"desc_path:",         # temp desc was written
            r"upload_test:",       # final result line printed
        ),
        benign_errors=_BENIGN,
        rc_ok=(0, 2),            # 0=flush ok; 2=flush timeout (no crash either way)
        run_no=1))
    tail = r["output"][-1500:]
    assert v["ok"], "; ".join(v["reasons"]) + f"\n--- device output tail ---\n{tail}"
