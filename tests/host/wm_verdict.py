"""Deterministic verdict for one `-wm 0` record run (record-specific facade).

Pure (no pytest, no device) so the verdict logic that Level-2 "propose fix"
trusts can be unit-tested independently. Both the hardware test
(test_wm_repeat.py) and the pure unit test (test_verdict.py) call `check_run`.

Reuses scenario_verdict helpers (ANSI strip / non-benign E/ filtering) but keeps
its own anchor + metric reason wording verbatim — that wording is pinned by
test_verdict.py. New non-record scenarios use scenario_verdict.judge directly.
"""
import re

from scenario_verdict import strip_ansi, nonbenign_error_lines

# App emits "RecordTask: record start: <path>" (record_task.cpp) / "Work Mode
# record start: <path>" (WorkModeRunner) — NOT "record started: file=". The
# trailing colon distinguishes the success line from "record start failed".
RECSTART_RE = re.compile(r"record start:")
# FPS anchors ARE real — VideoRecorder.cpp:686 (record stats) / :909 (record
# summary) both emit "observed_fps=<f>". Prefer the final summary (stable value).
SUMMARY_RE = re.compile(r"record summary:.*observed_fps=(\d+(?:\.\d+)?)")
STATS_RE   = re.compile(r"record stats:.*observed_fps=(\d+(?:\.\d+)?)")
# E/ lines expected in the devtest env (no mgmt/upload backend wired up) that say
# nothing about the record path under test — must NOT fail the verdict.
BENIGN_ERR = (r"UploadWorker: auth failed|no storage client",)


def check_run(output, rc, timed_out, run_no=0):
    """Return {"ok": bool, "reasons": [str], "observed_fps": float|None}.

    A run is OK iff: not timed out, rc == 0 (not 137 watchdog), recording
    actually started and produced frames, and no NON-benign EasyLogger E/ error
    lines (upload/auth errors are benign — no backend in the devtest env).

    Note on HTC_TEST_NO_POWEROFF: it is NOT checked via a log line — that line is
    buffered by EasyLogger and lost when the app calls _exit(0) (no flush). The
    flag's effect (board stayed alive) is *implied* by the sentinel firing: if
    the board had powered off, the trailing sentinel printf would never run and
    the run would time out.
    """
    output = strip_ansi(output)
    reasons = []
    fps = None

    if timed_out:
        reasons.append(f"run {run_no}: timed out (hang — IMP residue, or board "
                       f"powered off because HTC_TEST_NO_POWEROFF was not set)")
    if rc == 137:
        reasons.append(f"run {run_no}: rc=137 (watchdog SIGKILL — process hung)")
    elif rc is not None and rc != 0:
        reasons.append(f"run {run_no}: rc={rc} (non-zero exit)")

    if not RECSTART_RE.search(output):
        reasons.append(f"run {run_no}: missing 'record start:'")
    m = SUMMARY_RE.search(output) or STATS_RE.search(output)
    if not m:
        reasons.append(f"run {run_no}: missing 'record stats/summary ... observed_fps'")
    else:
        fps = float(m.group(1))

    err_lines = nonbenign_error_lines(output, BENIGN_ERR)
    if err_lines:
        reasons.append(f"run {run_no}: {len(err_lines)} non-benign E/ error line(s); "
                       f"first: {err_lines[0].strip()[:140]}")

    return {"ok": not reasons, "reasons": reasons, "observed_fps": fps}
