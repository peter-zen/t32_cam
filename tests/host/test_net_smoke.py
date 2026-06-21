"""Net smoke (Phase-1): the network function is up on the device.

Safe to run repeatedly (no IMP/SDK, no wedge risk) — so it's the first new
scenario to validate the devctl→verdict→device path on a non-record function.

Two checks:
  1. wlan0 has an IPv4 (the net module's outcome post-bringup).
  2. (optional, if HTC_WIFI_PWD set) the just-built htc_net_app from NFS exits
     cleanly when asked to REUSE the existing association (--no-dhcp). This
     exercises the binary without disrupting the link.

Run (broker up):
    HTC_WIFI_PWD=<pwd> pytest tests/host/test_net_smoke.py -s --junit-xml=logs/net_smoke.xml
"""
import os
import subprocess

import pytest

from scenario_verdict import judge, VerdictSpec

pytestmark = pytest.mark.hardware

# SSID by build-host subnet — mirrors tools/devctl/devctl ENVS (keep in sync).
_SSID_BY_SUBNET = {"192.168.31.": "no_mesh_02_2.4G", "192.168.0.": "no_mesh_01_2.4G"}


def _ssid():
    if os.environ.get("HTC_WIFI_SSID"):
        return os.environ["HTC_WIFI_SSID"]
    try:
        ips = subprocess.check_output(["hostname", "-I"], text=True)
    except Exception:
        return None
    for sub, ssid in _SSID_BY_SUBNET.items():
        if sub in ips:
            return ssid
    return None


def test_wlan0_has_ipv4(device):
    out = device.run("ifconfig wlan0 2>/dev/null | grep 'inet addr'", timeout=12)
    v = judge(out["output"], out["rc"], out["timed_out"], VerdictSpec(
        name="net-smoke/wlan0-ip",
        required_anchors=(r"inet addr:\d+\.\d+\.\d+\.\d+",),
        run_no=1))
    assert v["ok"], "; ".join(v["reasons"]) + f"\n--- ifconfig ---\n{out['output']}"


def test_net_app_reuse_exits_clean(device):
    """Run the NFS-deployed htc_net_app against the current SSID; with the link
    already up it takes the REUSE path and exits 0 (no disruption, no DHCP).
    Requires HTC_WIFI_PWD (+ SSID by subnet)."""
    pwd = os.environ.get("HTC_WIFI_PWD")
    ssid = _ssid()
    if not pwd or not ssid:
        pytest.skip("set HTC_WIFI_PWD (+ HTC_WIFI_SSID or home/company subnet)")
    r = device.run(
        f"LD_LIBRARY_PATH=/mnt/huntcam/lib:$LD_LIBRARY_PATH "
        f"/mnt/huntcam/bin/htc_net_app --ssid '{ssid}' --pwd '{pwd}' --no-dhcp",
        timeout=60)
    # rc 0 = success, 5 = connected but MCU write-back gated (still reachable)
    v = judge(r["output"], r["rc"], r["timed_out"], VerdictSpec(
        name="net-smoke/net-app-reuse",
        required_anchors=(),
        benign_errors=(r"writeUPID|writeUPWD|write-back|MCU",),
        rc_ok=(0, 5),
        run_no=1))
    assert v["ok"], "; ".join(v["reasons"]) + f"\n--- net_app tail ---\n{r['output'][-1200:]}"
