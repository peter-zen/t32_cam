"""Pure unit tests for the generic scenario_verdict.judge (no device, no broker).

Pins the shared rc/timeout/anchor/metric/error-line contract that the new
net/record-smoke/modes-matrix scenarios rely on.
"""
from scenario_verdict import judge, VerdictSpec


def _spec(**kw):
    base = dict(name="t", required_anchors=(r"HELLO",), metric_re=None,
                benign_errors=(), rc_ok=(0,), run_no=1)
    base.update(kw)
    return VerdictSpec(**base)


def test_good_run():
    v = judge("HELLO world\n", rc=0, timed_out=False, spec=_spec())
    assert v["ok"], v["reasons"]


def test_missing_anchor():
    v = judge("no greeting\n", rc=0, timed_out=False, spec=_spec())
    assert not v["ok"]
    assert any("missing anchor" in r for r in v["reasons"])


def test_metric_extracted():
    v = judge("HELLO speed=27.5 done\n", rc=0, timed_out=False,
              spec=_spec(metric_re=r"speed=(\d+(?:\.\d+)?)"))
    assert v["ok"], v["reasons"]
    assert v["metric"] == 27.5


def test_benign_error_tolerated():
    v = judge("HELLO\nE/NET auth failed\n", rc=0, timed_out=False,
              spec=_spec(benign_errors=(r"auth failed",)))
    assert v["ok"], v["reasons"]


def test_real_error_fails():
    v = judge("HELLO\nE/HAL IMP_Encoder_CreateChn(0) failed\n", rc=0,
              timed_out=False, spec=_spec())
    assert not v["ok"]
    assert any("IMP_Encoder_CreateChn" in r for r in v["reasons"])


def test_rc_not_in_ok():
    v = judge("HELLO\n", rc=2, timed_out=False, spec=_spec(rc_ok=(0, 5)))
    assert not v["ok"]
    assert any("rc=2" in r for r in v["reasons"])


def test_watchdog_and_timeout():
    assert not judge("HELLO\n", rc=137, timed_out=False, spec=_spec())["ok"]
    assert not judge("", rc=None, timed_out=True, spec=_spec())["ok"]


def test_ansi_stripped_from_anchors():
    v = judge("\x1b[36;22mHELLO\x1b[0m colored\n", rc=0, timed_out=False, spec=_spec())
    assert v["ok"], v["reasons"]
