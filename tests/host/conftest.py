"""Pytest config + fixtures for the host-side device test suite.

The `device` fixture yields a live DevCtl client and skips the session if the
broker / device isn't reachable — so `pytest` on a sim-only dev box doesn't
error on hardware tests.

Also emits a machine-readable summary at logs/devtest_report.json after every
session (Phase-1 "JSON report 汇总"): one row per test {name, outcome, duration}
+ pass/fail/skip counts. Verdict REASONS stay in the junit-xml failure messages;
this JSON is the at-a-glance summary the /devtest skill + Level-2 diagnose step read.
"""
import datetime
import json
import os
import sys

import pytest

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, "..", "..", "tools", "devctl"))
sys.path.insert(0, _HERE)  # so test files can `from wm_verdict import check_run`

from devctl_client import DevCtl, DevCtlError  # noqa: E402

_REPORT_PATH = os.path.join(_HERE, "..", "..", "logs", "devtest_report.json")
_RESULTS = []  # rows appended by pytest_runtest_makereport


def pytest_configure(config):
    config.addinivalue_line(
        "markers", "hardware: needs the T32 device + a running devctl broker")


@pytest.fixture(scope="session")
def device():
    c = DevCtl()
    try:
        c.status()
    except DevCtlError as e:
        pytest.skip(f"broker/device not available: {e}")
    return c


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    # Record the "call" phase verdict (the test body, not setup/teardown).
    if rep.when == "call":
        _RESULTS.append({
            "name": rep.nodeid,
            "outcome": rep.outcome,          # passed/failed/skipped-equivalent
            "duration_s": round(rep.duration, 3),
        })


def pytest_sessionfinish(session, exitstatus):
    """Write logs/devtest_report.json — best-effort, never fail the session over it."""
    try:
        summary = {"passed": 0, "failed": 0, "skipped": 0}
        for r in _RESULTS:
            summary[r["outcome"]] = summary.get(r["outcome"], 0) + 1
        summary["total"] = len(_RESULTS)
        report = {
            "generated_at": datetime.datetime.now().isoformat(timespec="seconds"),
            "iteration_cap_per_invocation": 3,  # SKILL guardrail: ≤3 runs/devtest
            "summary": summary,
            "tests": _RESULTS,
        }
        os.makedirs(os.path.dirname(_REPORT_PATH), exist_ok=True)
        with open(_REPORT_PATH, "w") as f:
            json.dump(report, f, indent=2)
    except Exception:
        pass  # reporting is non-critical; tests already have junit-xml
