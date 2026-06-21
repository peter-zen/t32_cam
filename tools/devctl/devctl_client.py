"""devctl Python client — talks to the broker over its Unix socket.

Used by the `devctl` CLI and by the pytest suite (tests/host/). Keeping the
transport in one place means the loop and the tests drive the device identically.
"""
import json
import os
import socket
import time

DEFAULT_SOCKET = "/tmp/devctl.sock"


class DevCtlError(RuntimeError):
    pass


class DevCtl:
    def __init__(self, sock_path=DEFAULT_SOCKET, timeout=None):
        self.sock_path = sock_path
        self.timeout = timeout

    def _request(self, req):
        if not os.path.exists(self.sock_path):
            raise DevCtlError(
                f"broker not running (no socket at {self.sock_path}); "
                f"start it with: python tools/devctl/broker.py   (or: devctl broker start)"
            )
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(self.timeout)
        try:
            s.connect(self.sock_path)
            s.sendall(json.dumps(req).encode("utf-8"))
            chunks = []
            while True:
                b = s.recv(65536)
                if not b:
                    break
                chunks.append(b)
            data = b"".join(chunks)
            if not data:
                raise DevCtlError("broker closed connection without a reply")
            return json.loads(data.decode("utf-8"))
        finally:
            s.close()

    # --- ops ---
    def run(self, cmd, timeout=30):
        """Run a shell command on the device. Returns
        {"rc": int|None, "output": str, "timed_out": bool}."""
        return self._request({"op": "run", "cmd": cmd, "timeout": timeout})

    def log_tail(self, n=50):
        return self._request({"op": "log", "n": n}).get("lines", [])

    def status(self):
        return self._request({"op": "status"})

    def send(self, data):
        return self._request({"op": "send", "data": data})

    def ctrl_c(self):
        return self._request({"op": "ctrl_c"})

    def reboot(self):
        return self._request({"op": "reboot"})

    def wait_boot(self, timeout=120):
        """Poll until the device shell responds (rc 0) after a reboot."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                if self.run("echo devctl_alive", timeout=12).get("rc") == 0:
                    return True
            except DevCtlError:
                pass
            time.sleep(2)
        return False
