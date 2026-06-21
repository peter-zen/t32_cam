"""Pure unit tests for the wm verdict logic (no device, no broker).

These run anywhere pytest runs and pin down exactly what counts as pass/fail —
the deterministic contract that Level-2 "propose fix" relies on.
"""
from wm_verdict import check_run, check_repeat_run

GOOD = (
    "I/LEGACY [..] main working mode 0, rtc status 1\n"
    "I/CamRec  [..] record start:/mnt/sdcard/media/x.mp4 duration=30s\n"
    "I/LEGACY  [..] record stats: frames=150 observed_fps=27.32\n"
    "I/LEGACY  [..] RecordTask: thumbnail saved for x.mp4 (9414 bytes)\n"
    "I/LEGACY  [..] [TEST] HTC_TEST_NO_POWEROFF set: _exit(0) instead of poweroff\n"
)


def test_good_run_passes():
    v = check_run(GOOD, rc=0, timed_out=False, run_no=1)
    assert v["ok"], v["reasons"]
    assert v["observed_fps"] == 27.32


def test_passes_without_flag_string():
    # HTC_TEST_NO_POWEROFF is no longer checked via its (un-flushed) log line;
    # a clean run with valid record stats + rc 0 passes even without the string.
    v = check_run(
        "record start:x\nrecord stats: observed_fps=20\nthumbnail saved\n",
        rc=0, timed_out=False)
    assert v["ok"], v["reasons"]


def test_error_line_detected_through_ansi_color():
    # The console wraps lines in ANSI color; E/ must still be detected.
    colored = "\x1b[36;22mI/LEGACY ok\n\x1b[31;22mE/LEGACY [..] broke\n"
    v = check_run(colored + "record start:x\nrecord stats: observed_fps=20\n",
                  rc=0, timed_out=False)
    assert not v["ok"]
    assert any("E/" in r for r in v["reasons"])


def test_benign_upload_auth_error_tolerated():
    # No mgmt/upload backend in the devtest env — UploadWorker auth failures are
    # expected and must not fail the verdict (they don't touch the record path).
    out = ("record start:x\nrecord summary: observed_fps=27.5\n"
           "thumbnail saved\n"
           "E/LEGACY [..] UploadWorker: auth failed\n")
    v = check_run(out, rc=0, timed_out=False)
    assert v["ok"], v["reasons"]
    assert v["observed_fps"] == 27.5


def test_real_encoder_error_fails():
    # IMP_Encoder_CreateChn failure (consecutive-record channel contention) is a
    # real defect the stabilization work targets — must fail.
    out = ("record start:a.mp4\nrecord summary: observed_fps=27.5\n"
           "E/LEGACY [HAL] configure: IMP_Encoder_CreateChn(0) failed\n"
           "E/CamRec record: start failed\n")
    v = check_run(out, rc=0, timed_out=False)
    assert not v["ok"]
    assert any("IMP_Encoder_CreateChn" in r for r in v["reasons"])


def test_error_line_fails():
    v = check_run(GOOD + "E/LEGACY [..] something broke\n", rc=0, timed_out=False)
    assert not v["ok"]
    assert any("E/" in r for r in v["reasons"])


def test_watchdog_rc_fails():
    v = check_run(GOOD, rc=137, timed_out=False)
    assert not v["ok"]
    assert any("137" in r for r in v["reasons"])


def test_nonzero_rc_fails():
    v = check_run(GOOD, rc=3, timed_out=False)
    assert not v["ok"]
    assert any("rc=3" in r for r in v["reasons"])


def test_timeout_fails():
    v = check_run("", rc=None, timed_out=True)
    assert not v["ok"]
    assert any("timed out" in r for r in v["reasons"])


def test_missing_record_stats_fails():
    out = "I/LEGACY [..] [TEST] HTC_TEST_NO_POWEROFF set\nI/CamRec record start:x\n"
    v = check_run(out, rc=0, timed_out=False)
    assert not v["ok"]
    assert any("record stats" in r for r in v["reasons"])


def test_missing_thumbnail_fails():
    # A record that started + produced fps but captured no thumbnail (CH2
    # concurrentSnap broke / saveThumbnail failed) must fail the verdict.
    out = "record start:x\nrecord stats: observed_fps=27.0\n"  # no 'thumbnail saved'
    v = check_run(out, rc=0, timed_out=False)
    assert not v["ok"]
    assert any("thumbnail" in r for r in v["reasons"])


# --- check_repeat_run (multi-segment verdict) ---

REPEAT_GOOD = (
    "I/LEGACY [..] RecordTask: record start: /m/a.mp4\n"
    "I/LEGACY [..] record summary: observed_fps=27.0\n"
    "I/LEGACY [..] RecordTask: record start: /m/b.mp4\n"
    "I/LEGACY [..] record summary: observed_fps=27.1\n"
    "I/LEGACY [..] RecordTask: record start: /m/c.mp4\n"
    "I/LEGACY [..] record summary: observed_fps=27.2\n"
)


def test_repeat_three_segments_passes():
    v = check_repeat_run(REPEAT_GOOD, rc=0, timed_out=False, expected_count=3)
    assert v["ok"], v["reasons"]
    assert v["segment_count"] == 3


def test_repeat_too_few_segments_fails():
    v = check_repeat_run(REPEAT_GOOD, rc=0, timed_out=False, expected_count=5)
    assert not v["ok"]
    assert any("only 3" in r for r in v["reasons"])


def test_repeat_encoder_createchn_fails():
    out = REPEAT_GOOD + "E/HAL configure: IMP_Encoder_CreateChn(0) failed\n"
    v = check_repeat_run(out, rc=0, timed_out=False, expected_count=3)
    assert not v["ok"]
    assert any("IMP_Encoder_CreateChn" in r for r in v["reasons"])


def test_repeat_does_not_count_failed_starts():
    # "record start failed" (no colon after start) must NOT count as a segment.
    out = ("record start: /m/a.mp4\nrecord start failed (rejected)\n"
           "record start: /m/b.mp4\n")
    v = check_repeat_run(out, rc=0, timed_out=False, expected_count=2)
    assert v["segment_count"] == 2
