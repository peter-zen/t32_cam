#!/usr/bin/env python3
r"""devctl broker — the single owner of the T32 serial console.

It does three jobs (see doc/knowledge/decisions/devtest-automation-loop.md §5/§6):
  1. Continuously reads /dev/ttyUSB0 and tees everything to logs/serial.log
     (always-on capture -> boot/crash/reboot logs land on disk).
  2. Accepts commands on a Unix socket and injects them on the serial line.
  3. For `run`, returns the command's real stdout + real exit code.

START/END marker scheme (robust against the shell echoing the injected command,
even when the echo wraps across terminal lines — observed on the T32 console):

  injected:   echo __S_<token>__; <cmd>; printf '__E_<token>_%d__\n' $?

The shell ECHOES the injected line (containing `__S_<token>__` followed by `;`
and `__E_<token>_%d__` with a literal %d). But `echo __S_<token>__` PRINTS the
real start marker on its own line, followed by `\n`; and `printf` prints the
real end marker with the exit code as digits. We therefore match

    __S_<token>__\n (.*?) __E_<token>_(\d+)__      (DOTALL, non-greedy)

The `\n` after the start marker and the `\d+` in the end marker mean neither the
echoed start (`;` after) nor the echoed end (`%d`) can match — so the captured
group(1) is the pristine command output and group(2) is the real exit code,
regardless of echo or line-wrapping. `stty -echo` is NOT relied upon (it proved
unreliable on this device).

The serial transport is isolated from the core logic so the core can be
self-tested without hardware (`python broker.py --self-test`).
"""
import argparse
import json
import os
import re
import socket
import sys
import threading
import time
import uuid

SOCK_DEFAULT = "/tmp/devctl.sock"
LOG_DEFAULT = "logs/serial.log"
BAUD_DEFAULT = 115200
RING_LINES = 4000

# Any line bearing a start/end marker (real or echoed) is dropped from serial.log
# so the persisted log stays clean of broker plumbing.
MARKER_LINE_RE = re.compile(r"__[SE]_[0-9a-f]{6,12}")
START_TMPL = "__S_{token}__"
# Real printf output: __E_<token>_<rc>__. Echoed form carries literal %d.
MATCH_TMPL = r"__S_{token}__\n(.*?)__E_{token}_(\d+)__"


# --------------------------------------------------------------------------- #
# Transports
# --------------------------------------------------------------------------- #
class SerialTransport:
    """Real /dev/ttyXXX transport via pyserial (imported lazily)."""

    def __init__(self, port, baud):
        self.port = port
        self.baud = baud
        self._ser = None

    def open(self):
        import serial  # lazy: not needed for self-test / unit use

        self._ser = serial.Serial(self.port, self.baud, bytesize=8,
                                  parity="N", stopbits=1, timeout=0.2)

    def read(self):
        return self._ser.read(1024) if self._ser else b""

    def write(self, data):
        if self._ser:
            self._ser.write(data)

    def close(self):
        if self._ser:
            self._ser.close()


class FakeTransport:
    """In-memory transport for self-test: `read()` drains a queue, `write()`
    records what the broker injected so the test can play the device's reply."""

    def __init__(self):
        self._q = []
        self._lock = threading.Lock()
        self.written = []

    def open(self):
        pass

    def feed(self, data):
        with self._lock:
            if isinstance(data, str):
                data = data.encode()
            self._q.append(data)

    def read(self):
        with self._lock:
            if self._q:
                return self._q.pop(0)
        time.sleep(0.005)
        return b""

    def write(self, data):
        self.written.append(data)

    def close(self):
        pass


# --------------------------------------------------------------------------- #
# Broker core
# --------------------------------------------------------------------------- #
class Broker:
    def __init__(self, transport, log_path):
        self.transport = transport
        self.log_path = log_path
        self._lock = threading.Lock()
        self._run_lock = threading.Lock()      # serializes run() (single loop)
        self._run = None                        # pending run dict or None
        self._lines = []                        # ring of recent log lines
        self._last_activity = 0.0
        self._stop = False
        os.makedirs(os.path.dirname(log_path) or ".", exist_ok=True)
        self._logf = open(log_path, "ab", buffering=0)

    # ----- reader thread: port -> log file + active-run matcher ----- #
    def _reader(self):
        while not self._stop:
            data = self.transport.read()
            if not data:
                continue
            self._last_activity = time.time()
            # The serial console emits CRLF; normalize to LF so marker regexes
            # (which anchor on "__S_<token>__\n") match. Bare CR -> dropped.
            text = data.decode("utf-8", "replace").replace("\r", "")
            self._tee(text)
            self._feed(text)

    def _tee(self, text):
        clean = [ln for ln in text.splitlines(keepends=True)
                 if not MARKER_LINE_RE.search(ln)]
        if not clean:
            return
        self._logf.write("".join(clean).encode("utf-8", "replace"))
        with self._lock:
            self._lines.extend("".join(clean).splitlines())
            if len(self._lines) > RING_LINES:
                del self._lines[: len(self._lines) - RING_LINES]

    def _feed(self, text):
        with self._lock:
            if not self._run or self._run["done"]:
                return
            self._run["output"] += text
            if self._run["regex"].search(self._run["output"]):
                self._run["done"] = True

    # ----- ops ----- #
    def run(self, cmd, timeout):
        with self._run_lock:
            token = uuid.uuid4().hex[:8]
            start = START_TMPL.format(token=token)
            match_re = re.compile(MATCH_TMPL.format(token=token), re.DOTALL)
            injected = f"echo {start}; {cmd}; printf '__E_{token}_%d__\\n' $?\n"
            with self._lock:
                self._run = {"regex": match_re, "rc": None,
                             "output": "", "done": False}
            self.transport.write(injected.encode())
            deadline = time.time() + timeout
            while time.time() < deadline:
                with self._lock:
                    if self._run["done"]:
                        break
                time.sleep(0.03)
            with self._lock:
                r, self._run = self._run, None
        m = match_re.search(r["output"])
        if m:
            return {"rc": int(m.group(2)), "output": m.group(1), "timed_out": False}
        # timeout: best-effort partial output after the real start marker
        out = r["output"]
        si = out.find(start + "\n")
        partial = out[si + len(start) + 1:] if si >= 0 else out
        return {"rc": None, "output": partial, "timed_out": True}

    def tail(self, n):
        with self._lock:
            return list(self._lines[-n:])

    def status(self):
        with self._lock:
            return {"alive": True, "log": os.path.abspath(self.log_path),
                    "port": getattr(self.transport, "port", "<fake>"),
                    "run_active": self._run is not None,
                    "last_activity": self._last_activity}

    def send_raw(self, data):
        self.transport.write(data.encode() if isinstance(data, str) else data)
        return {"ok": True}

    # ----- socket server ----- #
    def serve(self, sock_path):
        if os.path.exists(sock_path):
            os.remove(sock_path)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(sock_path)
        srv.listen(8)
        self._srv = srv
        while not self._stop:
            try:
                conn, _ = srv.accept()
            except OSError:
                break
            threading.Thread(target=self._handle, args=(conn,), daemon=True).start()

    def _handle(self, conn):
        try:
            req = json.loads(conn.recv(65536).decode("utf-8"))
            op = req.get("op")
            if op == "run":
                resp = self.run(req["cmd"], req.get("timeout", 30))
            elif op == "log":
                resp = {"lines": self.tail(int(req.get("n", 50)))}
            elif op == "status":
                resp = self.status()
            elif op == "send":
                resp = self.send_raw(req.get("data", ""))
            elif op == "ctrl_c":
                resp = self.send_raw("\x03")
            elif op == "reboot":
                resp = self.send_raw("reboot\n")
            else:
                resp = {"error": f"unknown op: {op}"}
        except Exception as e:  # never let one bad request kill the broker
            resp = {"error": repr(e)}
        finally:
            try:
                conn.sendall(json.dumps(resp).encode("utf-8"))
            finally:
                conn.close()


# --------------------------------------------------------------------------- #
# Self-test (no hardware, no pyserial)
# --------------------------------------------------------------------------- #
def _device_reply_once(ft, rc, stdout=""):
    """Play a realistic device session for the last injected command: echo the
    injected line, then the real start marker, cmd stdout, and end marker."""
    for _ in range(200):
        if ft.written:
            break
        time.sleep(0.01)
    injected = ft.written[-1].decode()   # reply to the LATEST injected command
    ft.written.clear()
    ft.feed(injected)                                  # shell echoes the command
    ms = re.search(r"echo (__S_[0-9a-f]{6,12}__)", injected)
    me = re.search(r"__E_([0-9a-f]{6,12})_%d__", injected)
    ft.feed(ms.group(1) + "\n")                        # real start marker
    if stdout:
        ft.feed(stdout)
    ft.feed(f"__E_{me.group(1)}_{rc}__\n")             # real end marker


def _self_test():
    ft = FakeTransport()
    br = Broker(ft, "logs/serial.selftest.log")
    threading.Thread(target=br._reader, daemon=True).start()

    ft.written.clear()
    threading.Thread(target=lambda: _device_reply_once(ft, 0, "hello\n"),
                     daemon=True).start()
    res = br.run("echo hello", timeout=3)
    assert not res["timed_out"], "timed out waiting for end marker"
    assert res["rc"] == 0, f"expected rc 0, got {res['rc']}"
    assert res["output"] == "hello\n", f"output: {res['output']!r}"
    assert "__S_" not in res["output"] and "__E_" not in res["output"], "marker leaked"

    # timeout path (no device reply)
    ft.written.clear()
    res2 = br.run("sleep 999", timeout=0.4)
    assert res2["timed_out"], "expected timeout"
    assert res2["rc"] is None

    # non-zero exit code (clear first so the helper only sees THIS command)
    ft.written.clear()
    threading.Thread(target=lambda: _device_reply_once(ft, 7), daemon=True).start()
    res3 = br.run("(exit 7)", timeout=3)
    assert res3["rc"] == 7, f"expected rc 7, got {res3['rc']}"

    print("broker self-test PASSED")


def main():
    ap = argparse.ArgumentParser(description="devctl serial broker")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=BAUD_DEFAULT)
    ap.add_argument("--socket", default=SOCK_DEFAULT)
    ap.add_argument("--log", default=LOG_DEFAULT)
    ap.add_argument("--self-test", action="store_true",
                    help="run no-hardware self-test and exit")
    args = ap.parse_args()

    if args.self_test:
        _self_test()
        return

    transport = SerialTransport(args.port, args.baud)
    try:
        transport.open()
    except Exception as e:
        sys.exit(f"broker: cannot open {args.port}: {e}\n"
                 f"  (WSL2: is the USB-UART passed through? try usbipd-win attach)")
    br = Broker(transport, args.log)
    threading.Thread(target=br._reader, daemon=True).start()
    with open("/tmp/devctl.broker.pid", "w") as pf:
        pf.write(str(os.getpid()))
    print(f"devctl broker: port={args.port} log={args.log} socket={args.socket}",
          flush=True)
    try:
        br.serve(args.socket)
    except KeyboardInterrupt:
        pass
    finally:
        for path in ("/tmp/devctl.broker.pid", args.socket):
            try:
                os.remove(path)
            except OSError:
                pass


if __name__ == "__main__":
    main()
