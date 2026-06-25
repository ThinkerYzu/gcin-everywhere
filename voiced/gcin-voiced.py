#!/usr/bin/env python3
"""
gcin-voiced — ASR daemon for the gcin-everywhere voice input method.

Phase A of the voice-input design (research/VOICE-INPUT-DESIGN.md). This is a
standalone daemon that owns the microphone and holds MediaTek Breeze-ASR-26 in
memory; the IBus engine talks to it as a thin control client over a Unix domain
socket. It wraps the already-validated POC transcription path
(research/poc/breeze_asr_mic.py) as a long-lived socket server.

Boundary contract (the only thing the engine depends on):

    socket : $XDG_RUNTIME_DIR/gcin-everywhere/voiced.sock
    framing: newline-delimited JSON, one object per line, UTF-8

  engine -> daemon (commands)
    {"cmd":"ping"}                          liveness / trigger lazy model load
    {"cmd":"start"}                         begin mic capture
    {"cmd":"stop"}                          end capture, transcribe, return text
    {"cmd":"cancel"}                        drop capture, no transcription
    {"cmd":"config","language":"chinese","task":"transcribe"}

  daemon -> engine (events)
    {"event":"ready","model":"...","device":"cuda:0"}
    {"event":"recording"}                   mic is live   (engine shows 🎤)
    {"event":"thinking"}                    transcribing  (engine shows …)
    {"event":"transcript","text":"...","alts":[...],"rtf":0.3}
    {"event":"error","msg":"..."}

State machine (daemon): idle -> recording -> thinking -> idle.
`cancel` returns to idle from recording.

Design intent (see design doc):
  - Model loads once, lazily (on first `ping`/`start`), in a background thread so
    a non-voice session pays only the idle socket cost.
  - The accept/read loop never blocks on inference: transcription runs in a worker
    thread, so `cancel` stays responsive while a previous turn is still decoding.
  - The backend is swappable behind this socket. Phase A = HF Transformers (here);
    Phase B = whisper.cpp/GGML, with zero engine changes.

Run:
    gcin-voiced.py                 # real backend (Breeze-ASR-26 via Transformers)
    gcin-voiced.py --device cuda   # force GPU
    gcin-voiced.py --mock          # no model/mic; canned transcript (engine tests)
    gcin-voiced.py --list-devices  # show input devices and exit

Requires (real mode): transformers, torch, sounddevice + libportaudio2, numpy.
Mock mode needs none of these — it exists to develop/test the engine side.
"""

import argparse
import json
import os
import socket
import sys
import threading
import time

MODEL_ID = "MediaTek-Research/Breeze-ASR-26"
TARGET_SR = 16000          # Whisper works at 16 kHz mono
MIN_AUDIO_S = 0.2          # ignore sub-200ms blips (matches the POC)
MOCK_TRANSCRIPT = "語音輸入測試 你好世界"   # canned text for --mock


def log(*a):
    """Progress/diagnostics go to stderr (stdout is unused by the protocol)."""
    print("[gcin-voiced]", *a, file=sys.stderr, flush=True)


def default_socket_path():
    runtime = os.environ.get("XDG_RUNTIME_DIR") or f"/run/user/{os.getuid()}"
    return os.path.join(runtime, "gcin-everywhere", "voiced.sock")


# ── ASR backend ──────────────────────────────────────────────────────────
#
# Encapsulates the Breeze-ASR-26 model + microphone. Everything heavy
# (torch/transformers/sounddevice import, model load, CUDA init) is deferred
# until first use so the daemon idles cheaply. The Server below only ever calls
# the small method surface: load(), start_record(), stop_record()->audio,
# cancel_record(), transcribe(audio)->(text, alts, rtf).

class AsrBackend:
    def __init__(self, device="auto", device_index=None,
                 language="chinese", task="transcribe"):
        self._device = device
        self._device_index = device_index
        self.language = language
        self.task = task

        self._asr = None              # transformers pipeline (lazy)
        self._device_label = "cpu"
        self._load_lock = threading.Lock()

        self._stream = None           # sounddevice InputStream while recording
        self._chunks = []             # captured float32 frames

    # -- model lifecycle --
    @property
    def loaded(self):
        return self._asr is not None

    @property
    def device_label(self):
        return self._device_label

    def load(self):
        """Load the model once (idempotent, thread-safe). Blocks the caller."""
        if self._asr is not None:
            return
        with self._load_lock:
            if self._asr is not None:
                return
            import torch
            from transformers import pipeline
            if self._device == "auto":
                dev = 0 if torch.cuda.is_available() else -1
            elif self._device == "cuda":
                if not torch.cuda.is_available():
                    raise RuntimeError("no CUDA device available")
                dev = 0
            else:
                dev = -1
            self._device_label = "cuda:0" if dev == 0 else "cpu"
            log(f"loading {MODEL_ID} on {self._device_label} ...")
            t0 = time.time()
            self._asr = pipeline("automatic-speech-recognition",
                                 model=MODEL_ID, device=dev, chunk_length_s=30)
            log(f"model ready ({time.time() - t0:.1f}s)")

    # -- microphone capture (daemon owns the mic; engine never sees PCM) --
    def start_record(self):
        import sounddevice as sd
        self._chunks = []

        def cb(indata, _frames, _t, status):
            if status:
                log("audio status:", status)
            self._chunks.append(indata.copy())

        self._stream = sd.InputStream(samplerate=TARGET_SR, channels=1,
                                      dtype="float32",
                                      device=self._device_index, callback=cb)
        self._stream.start()

    def _close_stream(self):
        if self._stream is not None:
            try:
                self._stream.stop()
                self._stream.close()
            finally:
                self._stream = None

    def stop_record(self):
        """Stop capture and return the recorded float32 mono @ 16 kHz."""
        import numpy as np
        self._close_stream()
        if not self._chunks:
            return np.zeros(0, dtype="float32")
        return np.concatenate(self._chunks).reshape(-1)

    def cancel_record(self):
        self._close_stream()
        self._chunks = []

    # -- inference --
    def transcribe(self, audio):
        """Return (text, alts, rtf) for float32 mono @ 16 kHz audio."""
        self.load()
        dur = len(audio) / float(TARGET_SR)
        gen = {"language": self.language, "task": self.task}
        t0 = time.time()
        text = self._asr(audio, generate_kwargs=gen)["text"].strip()
        elapsed = time.time() - t0
        rtf = round(elapsed / dur, 3) if dur > 0 else 0.0
        return text, [], rtf


class MockBackend:
    """No model, no mic — returns a canned transcript. Lets the IBus engine and
    the socket protocol be developed/tested without a GPU or microphone."""
    def __init__(self, **_):
        self.language = "chinese"
        self.task = "transcribe"
        self._device_label = "mock"

    @property
    def loaded(self):
        return True

    @property
    def device_label(self):
        return "mock"

    def load(self):
        pass

    def start_record(self):
        pass

    def stop_record(self):
        return [0.0] * int(TARGET_SR)   # pretend ~1s captured

    def cancel_record(self):
        pass

    def transcribe(self, _audio):
        time.sleep(0.4)                  # simulate inference latency
        return MOCK_TRANSCRIPT, [], 0.1


# ── Socket server ────────────────────────────────────────────────────────
#
# One connected engine client at a time (the engine connects on entering voice
# mode and may reconnect across focus churn). The model stays loaded across
# connections. A single connection is handled by handle_conn(); the read loop
# stays responsive while a worker thread transcribes.

class Server:
    STATE_IDLE = "idle"
    STATE_RECORDING = "recording"
    STATE_THINKING = "thinking"

    def __init__(self, backend, sock_path):
        self.backend = backend
        self.sock_path = sock_path
        self.state = self.STATE_IDLE
        self._lock = threading.Lock()     # guards state + socket writes
        self._conn = None

    # -- framed I/O --
    def _send(self, **obj):
        """Send one JSON event line to the current client (thread-safe)."""
        with self._lock:
            conn = self._conn
            if conn is None:
                return
            try:
                conn.sendall((json.dumps(obj, ensure_ascii=False) + "\n").encode())
            except OSError:
                pass

    # -- command handlers --
    def _on_ping(self):
        # Warm the model in the background; report ready when it's actually up.
        def warm():
            try:
                self.backend.load()
                self._send(event="ready", model=MODEL_ID,
                           device=self.backend.device_label)
            except Exception as e:        # noqa: BLE001 - report any load failure
                self._send(event="error", msg=f"model load failed: {e}")
        if self.backend.loaded:
            self._send(event="ready", model=MODEL_ID,
                       device=self.backend.device_label)
        else:
            threading.Thread(target=warm, daemon=True).start()

    def _on_start(self):
        with self._lock:
            if self.state != self.STATE_IDLE:
                return
            self.state = self.STATE_RECORDING
        try:
            self.backend.start_record()
        except Exception as e:            # noqa: BLE001
            with self._lock:
                self.state = self.STATE_IDLE
            self._send(event="error", msg=f"mic capture failed: {e}")
            return
        self._send(event="recording")

    def _on_stop(self):
        with self._lock:
            if self.state != self.STATE_RECORDING:
                return
            self.state = self.STATE_THINKING
        try:
            audio = self.backend.stop_record()
        except Exception as e:            # noqa: BLE001
            with self._lock:
                self.state = self.STATE_IDLE
            self._send(event="error", msg=f"capture stop failed: {e}")
            return
        if len(audio) < TARGET_SR * MIN_AUDIO_S:
            with self._lock:
                self.state = self.STATE_IDLE
            self._send(event="transcript", text="", alts=[], rtf=0.0)
            return
        self._send(event="thinking")
        threading.Thread(target=self._transcribe_worker,
                         args=(audio,), daemon=True).start()

    def _transcribe_worker(self, audio):
        try:
            text, alts, rtf = self.backend.transcribe(audio)
            self._send(event="transcript", text=text, alts=alts, rtf=rtf)
        except Exception as e:            # noqa: BLE001
            self._send(event="error", msg=f"transcription failed: {e}")
        finally:
            with self._lock:
                self.state = self.STATE_IDLE

    def _on_cancel(self):
        with self._lock:
            if self.state != self.STATE_RECORDING:
                return
            self.state = self.STATE_IDLE
        self.backend.cancel_record()

    def _on_config(self, msg):
        lang = msg.get("language")
        task = msg.get("task")
        if lang:
            self.backend.language = lang
        if task:
            self.backend.task = task

    def _dispatch(self, msg):
        cmd = msg.get("cmd")
        if cmd == "ping":
            self._on_ping()
        elif cmd == "start":
            self._on_start()
        elif cmd == "stop":
            self._on_stop()
        elif cmd == "cancel":
            self._on_cancel()
        elif cmd == "config":
            self._on_config(msg)
        else:
            self._send(event="error", msg=f"unknown cmd: {cmd!r}")

    # -- connection + accept loops --
    def handle_conn(self, conn):
        with self._lock:
            self._conn = conn
        buf = b""
        try:
            while True:
                data = conn.recv(4096)
                if not data:
                    break
                buf += data
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        msg = json.loads(line.decode("utf-8"))
                    except (ValueError, UnicodeDecodeError):
                        self._send(event="error", msg="malformed JSON")
                        continue
                    self._dispatch(msg)
        finally:
            # Client gone: abort any in-flight recording, drop the conn ref.
            if self.state == self.STATE_RECORDING:
                self.backend.cancel_record()
                with self._lock:
                    self.state = self.STATE_IDLE
            with self._lock:
                self._conn = None
            try:
                conn.close()
            except OSError:
                pass

    def serve(self):
        os.makedirs(os.path.dirname(self.sock_path), mode=0o700, exist_ok=True)
        try:
            os.unlink(self.sock_path)
        except FileNotFoundError:
            pass
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(self.sock_path)
        os.chmod(self.sock_path, 0o600)
        srv.listen(1)
        log(f"listening on {self.sock_path}")
        try:
            while True:
                conn, _ = srv.accept()
                self.handle_conn(conn)     # one client at a time
        except KeyboardInterrupt:
            log("shutting down")
        finally:
            srv.close()
            try:
                os.unlink(self.sock_path)
            except FileNotFoundError:
                pass


def parse_args():
    p = argparse.ArgumentParser(description="gcin-everywhere ASR daemon (Breeze-ASR-26)")
    p.add_argument("--socket", default=None,
                   help="Unix socket path (default: $XDG_RUNTIME_DIR/gcin-everywhere/voiced.sock)")
    p.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"],
                   help="model inference device")
    p.add_argument("--device-index", type=int, default=None,
                   help="audio input device index (see --list-devices)")
    p.add_argument("--language", default="chinese", help="Whisper decode language hint")
    p.add_argument("--task", default="transcribe", choices=["transcribe", "translate"])
    p.add_argument("--mock", action="store_true",
                   help="no model/mic; return a canned transcript (engine tests)")
    p.add_argument("--list-devices", action="store_true",
                   help="list audio input devices and exit")
    return p.parse_args()


def main():
    args = parse_args()
    if args.list_devices:
        import sounddevice as sd
        print(sd.query_devices())
        return
    sock_path = args.socket or default_socket_path()
    if args.mock:
        backend = MockBackend()
        log("mock backend (no model, no mic)")
    else:
        backend = AsrBackend(device=args.device, device_index=args.device_index,
                             language=args.language, task=args.task)
    Server(backend, sock_path).serve()


if __name__ == "__main__":
    main()
