#!/usr/bin/env python3
"""Protocol smoke test for gcin-voiced against the --mock backend.

Spawns the daemon in mock mode on a temp socket, drives a full
ping -> start -> stop -> transcript exchange plus cancel, and asserts the
event sequence. No model or microphone required, so it runs anywhere
(CI, dev box). Exits non-zero on any mismatch.

    python3 test-protocol.py
"""
import json
import os
import socket
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
DAEMON = os.path.join(HERE, "gcin-voiced.py")


class Client:
    def __init__(self, path):
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(path)
        self.s.settimeout(10)
        self.buf = b""

    def send(self, **obj):
        self.s.sendall((json.dumps(obj) + "\n").encode())

    def recv_event(self):
        while b"\n" not in self.buf:
            data = self.s.recv(4096)
            if not data:
                raise EOFError("daemon closed connection")
            self.buf += data
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line.decode())

    def expect(self, event):
        ev = self.recv_event()
        assert ev.get("event") == event, f"expected {event!r}, got {ev!r}"
        print(f"  ok: {ev}")
        return ev


def wait_for_socket(path, timeout=10):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path):
            return True
        time.sleep(0.05)
    return False


def main():
    tmp = tempfile.mkdtemp(prefix="gcin-voiced-test-")
    sock = os.path.join(tmp, "voiced.sock")
    proc = subprocess.Popen([sys.executable, DAEMON, "--mock", "--socket", sock],
                            stderr=subprocess.PIPE)
    try:
        if not wait_for_socket(sock):
            err = proc.stderr.read().decode() if proc.stderr else ""
            raise SystemExit(f"daemon did not create socket\n{err}")

        c = Client(sock)

        print("[1] ping -> ready")
        c.send(cmd="ping")
        ev = c.expect("ready")
        assert ev["device"] == "mock", ev

        print("[2] config (no reply expected)")
        c.send(cmd="config", language="chinese", task="transcribe")

        print("[3] start -> recording")
        c.send(cmd="start")
        c.expect("recording")

        print("[4] stop -> thinking -> transcript")
        c.send(cmd="stop")
        c.expect("thinking")
        ev = c.expect("transcript")
        assert ev["text"], "expected non-empty mock transcript"

        print("[5] start then cancel (no transcript), then a clean cycle")
        c.send(cmd="start")
        c.expect("recording")
        c.send(cmd="cancel")
        # cancel emits nothing; verify the daemon is back to idle by running
        # another full cycle.
        c.send(cmd="start")
        c.expect("recording")
        c.send(cmd="stop")
        c.expect("thinking")
        c.expect("transcript")

        print("[6] unknown cmd -> error")
        c.send(cmd="bogus")
        ev = c.expect("error")
        assert "bogus" in ev["msg"], ev

        print("\nALL PROTOCOL TESTS PASSED")
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    main()
