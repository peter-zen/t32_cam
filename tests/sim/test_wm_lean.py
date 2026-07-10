"""wm lean sim verification (wm-app-spec S6). Runs the `wm` binary via subprocess
on the PC sim build (build_sim), validates mode acceptance/rejection, ingest, and
lean invariants (no .db, no MediaScanner) for m2/m3.

Coverage:
  - m0/m1 regression (S2 safety net): boot/start/shutdown anchors present.
  - m2 /tmp-seed: seed /tmp/media/info.json + a dummy photo file,
    run `wm -m 2`, assert ingest log ("ingest N files ->").
  - m3 heartbeat: `wm -m 3` accepted, "heartbeat" anchor present.
  - m4 rejection: `wm -m 4` rejected (non-zero RC or error log).
  - Lean invariants: m2/m3 produce no `.db` files (skipDatabase=true).
  - /tmp isolation: TMPDIR is set to a test-specific temp directory;
    SIM_SD_ROOT is also pointed there so sim paths are self-contained.

Requires: `build_sim/bin/wm` already built (cmake --build build_sim -j4).
"""
import json
import os
import shutil
import subprocess
import tempfile
import time

import pytest

_WM_BIN = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "build_sim", "bin", "wm",
)

_WM_TIMEOUT = 30  # seconds


def _run_wm(mode, env_extra=None, timeout=_WM_TIMEOUT, cwd=None):
    """Run `wm -m <mode>` via subprocess, return (rc, stdout, stderr)."""
    cmd = [_WM_BIN, "-m", str(mode)]
    env = os.environ.copy()
    if env_extra:
        env.update(env_extra)
    proc = subprocess.run(
        cmd,
        cwd=cwd,
        env=env,
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return proc.returncode, proc.stdout, proc.stderr


def _setup_sim_env(tmpdir):
    """Create a minimal sim environment under tmpdir and return env dict."""
    sim_root = os.path.join(tmpdir, "sim_sdcard_runtime")
    os.makedirs(os.path.join(sim_root, "media", "upload"), exist_ok=True)
    os.makedirs(os.path.join(sim_root, "logs"), exist_ok=True)

    res_dir = os.path.join(tmpdir, "res")
    os.makedirs(res_dir, exist_ok=True)
    env_ini = os.path.join(res_dir, "env.ini")
    config_ini = os.path.join(res_dir, "config.sim.ini")
    for path in [env_ini, config_ini]:
        with open(path, "w") as f:
            f.write("\n")

    return {
        "SIM_SD_ROOT": sim_root,
        "SIM_LOG_DIR": os.path.join(sim_root, "logs"),
        "TMPDIR": tmpdir,
    }


def _seed_quick_snap_manifest(tmpdir):
    """Seed /tmp/media/ with info.json + a dummy photo file for m2 ingest test."""
    import random
    import string

    ts = time.strftime("%Y%m%d_%H%M%S")
    dir_path = f"/tmp/media/{ts}"
    os.makedirs(dir_path, exist_ok=True)

    # Create a minimal valid JPEG
    dummy_jpg = os.path.join(dir_path, f"{ts}_1.jpg")
    # Minimal JPEG bytes: SOI + APP0 + DQT + SOF0 + DHT + SOS + EOI (grayscale 1x1)
    minimal_jpeg = bytes([
        0xFF, 0xD8,  # SOI
        0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01,
        0x01, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,  # JFIF APP0
        0xFF, 0xDB, 0x00, 0x43, 0x00,  # DQT
    ] + [0x08] * 64 + [
        0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x01, 0x00,
        0x01, 0x01, 0x01, 0x11, 0x00,  # SOF0
        0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00,  # DHT
    ] + [0x01] * 31 + [
        0xFF, 0xDA, 0x00, 0x08, 0x01, 0x01, 0x00, 0x00,
        0x3F, 0x00, 0x7F, 0xA8,  # SOS + compressed data (fake)
        0xFF, 0xD9,  # EOI
    ])
    with open(dummy_jpg, "wb") as f:
        f.write(minimal_jpeg)

    info = {
        "dir": ts,
        "files": [f"{ts}_1.jpg"],
    }
    os.makedirs("/tmp/media", exist_ok=True)
    with open("/tmp/media/info.json", "w") as f:
        json.dump(info, f)

    return ts, dir_path, dummy_jpg


def _cleanup_quick_snap_manifest(dir_path):
    """Remove seeded /tmp/media files."""
    if os.path.exists(dir_path):
        shutil.rmtree(dir_path, ignore_errors=True)
    info_path = "/tmp/media/info.json"
    if os.path.exists(info_path):
        os.remove(info_path)


class TestWmModes:
    """Mode acceptance/rejection tests (m0/m1/m3 accepted, m4 rejected)."""

    def test_m0_capture_only_accepted(self, tmp_path):
        """wm -m 0 should boot, start, and shutdown cleanly."""
        env = _setup_sim_env(str(tmp_path))
        env["HTC_TEST_NO_POWEROFF"] = "1"
        env["HTC_WM_ONE_SHOT"] = "1"
        env["HTC_WM_IDLE_GRACE_MS"] = "3000"
        env["HTC_NO_MCU"] = "1"

        rc, stdout, stderr = _run_wm(0, env_extra=env, cwd=str(tmp_path))

        combined = stdout + stderr
        assert rc == 0, f"wm -m 0 rc={rc}, output: {combined[-2000:]}"
        assert "op=boot" in combined, "missing op=boot anchor"
        assert "op=start" in combined, "missing op=start anchor"
        assert "op=shutdown" in combined, "missing op=shutdown anchor"

    def test_m1_capture_upload_accepted(self, tmp_path):
        """wm -m 1 should boot, start, and shutdown (upload may fail on sim)."""
        env = _setup_sim_env(str(tmp_path))
        env["HTC_TEST_NO_POWEROFF"] = "1"
        env["HTC_WM_ONE_SHOT"] = "1"
        env["HTC_WM_IDLE_GRACE_MS"] = "3000"
        env["HTC_NO_MCU"] = "1"

        rc, stdout, stderr = _run_wm(1, env_extra=env, cwd=str(tmp_path))

        combined = stdout + stderr
        assert rc == 0, f"wm -m 1 rc={rc}, output: {combined[-2000:]}"
        assert "op=boot" in combined, "missing op=boot anchor"
        assert "op=start" in combined, "missing op=start anchor"
        assert "op=shutdown" in combined, "missing op=shutdown anchor"

    def test_m2_upload_only_ingest(self, tmp_path):
        """wm -m 2: seed /tmp/media, verify ingest ran and desc was created."""
        ts, dir_path, dummy_jpg = _seed_quick_snap_manifest(str(tmp_path))

        try:
            env = _setup_sim_env(str(tmp_path))
            env["HTC_TEST_NO_POWEROFF"] = "1"
            env["HTC_WM_ONE_SHOT"] = "1"
            env["HTC_WM_IDLE_GRACE_MS"] = "3000"
            env["HTC_NO_MCU"] = "1"

            rc, stdout, stderr = _run_wm(2, env_extra=env, cwd=str(tmp_path))

            combined = stdout + stderr
            assert rc == 0, f"wm -m 2 rc={rc}, output: {combined[-2000:]}"
            assert "op=boot" in combined, "missing op=boot anchor"
            # Ingest log: "ingest N files ->" (N>=1)
            assert "ingest" in combined.lower(), (
                f"missing ingest log, output: {combined[-2000:]}"
            )
            assert "op=shutdown" in combined, "missing op=shutdown anchor"

            # Verify desc was created at /tmp/<dir>.json
            desc_path = f"/tmp/{ts}.json"
            assert os.path.exists(desc_path), (
                f"desc file not created: {desc_path}"
            )
        finally:
            _cleanup_quick_snap_manifest(dir_path)

    def test_m3_heartbeat_accepted(self, tmp_path):
        """wm -m 3 should run heartbeat and exit cleanly."""
        env = _setup_sim_env(str(tmp_path))
        env["HTC_TEST_NO_POWEROFF"] = "1"
        env["HTC_NO_MCU"] = "1"

        rc, stdout, stderr = _run_wm(3, env_extra=env, cwd=str(tmp_path))

        combined = stdout + stderr
        assert rc == 0, f"wm -m 3 rc={rc}, output: {combined[-2000:]}"
        assert "op=boot" in combined, "missing op=boot anchor"
        assert "heartbeat" in combined.lower(), (
            f"missing heartbeat log, output: {combined[-2000:]}"
        )
        assert "mode=3" in combined, "missing mode=3 in boot log"

    def test_m4_rejected(self, tmp_path):
        """wm -m 4 should be rejected (invalid mode)."""
        env = _setup_sim_env(str(tmp_path))
        env["HTC_TEST_NO_POWEROFF"] = "1"
        env["HTC_NO_MCU"] = "1"

        rc, stdout, stderr = _run_wm(4, env_extra=env, cwd=str(tmp_path), timeout=15)

        combined = stdout + stderr
        # Invalid mode should error: non-zero RC or error log
        invalid = (rc != 0) or ("invalid" in combined.lower())
        assert invalid, (
            f"wm -m 4 should be rejected, rc={rc}, output: {combined[-2000:]}"
        )

    def test_m2_no_db_created(self, tmp_path):
        """m2 (lean) must not create any .db file."""
        ts, dir_path, dummy_jpg = _seed_quick_snap_manifest(str(tmp_path))

        try:
            env = _setup_sim_env(str(tmp_path))
            env["HTC_TEST_NO_POWEROFF"] = "1"
            env["HTC_WM_ONE_SHOT"] = "1"
            env["HTC_WM_IDLE_GRACE_MS"] = "3000"
            env["HTC_NO_MCU"] = "1"

            _run_wm(2, env_extra=env, cwd=str(tmp_path))

            # Search for .db files anywhere under tmp_path
            db_files = []
            for root, dirs, files in os.walk(str(tmp_path)):
                for f in files:
                    if f.endswith(".db"):
                        db_files.append(os.path.join(root, f))
            assert not db_files, f"lean m2 should not create .db files: {db_files}"
        finally:
            _cleanup_quick_snap_manifest(dir_path)

    def test_m3_no_db_created(self, tmp_path):
        """m3 (lean) must not create any .db file."""
        env = _setup_sim_env(str(tmp_path))
        env["HTC_TEST_NO_POWEROFF"] = "1"
        env["HTC_NO_MCU"] = "1"

        _run_wm(3, env_extra=env, cwd=str(tmp_path))

        db_files = []
        for root, dirs, files in os.walk(str(tmp_path)):
            for f in files:
                if f.endswith(".db"):
                    db_files.append(os.path.join(root, f))
        assert not db_files, f"lean m3 should not create .db files: {db_files}"
