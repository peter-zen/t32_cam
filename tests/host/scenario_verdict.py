"""Generic deterministic verdict for one device run + shared helpers.

Pure (no pytest, no device) so the verdict logic Level-2 "propose fix" trusts can
be unit-tested independently. Each NEW scenario (net/record-smoke/modes-matrix)
declares a VerdictSpec and calls judge(); the record-specific facade
wm_verdict.check_run reuses strip_ansi / nonbenign_error_lines so its pinned unit
tests (tests/host/test_verdict.py) keep their exact wording.

EasyLogger console format: "<level>/<tag> [ts] msg", wrapped in ANSI color.
"""
import re

_ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def strip_ansi(s):
    return _ANSI_RE.sub("", s or "")


def nonbenign_error_lines(output, benign_patterns=()):
    """E/ lines that do NOT match any benign pattern.

    EasyLogger errors start the line with 'E/'. In the devtest env some E/ lines
    are expected (no mgmt/upload backend) and must not fail the verdict — list
    them as benign regexes.
    """
    out = strip_ansi(output)
    return [ln for ln in out.split("\n")
            if ln.lstrip().startswith("E/")
            and not any(re.search(p, ln) for p in benign_patterns)]


class VerdictSpec:
    """Declarative pass criteria for one scenario run.

    required_anchors : regexes that MUST all appear in the (ANSI-stripped) output.
    metric_re        : optional regex with one capture group → extracted metric
                       (e.g. observed_fps). Absence is NOT a failure by itself;
                       add an anchor if presence is required.
    benign_errors    : regexes; E/ lines matching any are tolerated.
    rc_ok            : acceptable exit codes (137 and timeout never pass).
    """
    __slots__ = ("name", "required_anchors", "metric_re", "benign_errors",
                 "rc_ok", "run_no")

    def __init__(self, name, required_anchors=(), metric_re=None,
                 benign_errors=(), rc_ok=(0,), run_no=0):
        self.name = name
        self.required_anchors = required_anchors
        self.metric_re = metric_re
        self.benign_errors = benign_errors
        self.rc_ok = tuple(rc_ok)
        self.run_no = run_no


def judge(output, rc, timed_out, spec):
    """Apply spec to one run. Returns {"ok", "reasons", "metric"}."""
    out = strip_ansi(output)
    reasons = []
    metric = None

    if timed_out:
        reasons.append(f"run {spec.run_no}: timed out (hang — IMP residue, or board "
                       f"powered off because HTC_TEST_NO_POWEROFF was not set)")
    if rc == 137:
        reasons.append(f"run {spec.run_no}: rc=137 (watchdog SIGKILL — process hung)")
    elif rc is not None and rc not in spec.rc_ok:
        reasons.append(f"run {spec.run_no}: rc={rc} (exit not in ok={spec.rc_ok})")

    for anc in spec.required_anchors:
        if not re.search(anc, out):
            reasons.append(f"run {spec.run_no}: {spec.name} missing anchor /{anc}/")

    if spec.metric_re:
        m = re.search(spec.metric_re, out)
        if m and m.groups():
            try:
                metric = float(m.group(1))
            except (ValueError, IndexError):
                metric = m.group(1)

    errs = nonbenign_error_lines(out, spec.benign_errors)
    if errs:
        reasons.append(f"run {spec.run_no}: {len(errs)} non-benign E/ error line(s); "
                       f"first: {errs[0].strip()[:140]}")

    return {"ok": not reasons, "reasons": reasons, "metric": metric}
