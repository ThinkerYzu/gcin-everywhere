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
    {"cmd":"config","language":"chinese","task":"transcribe","punctuate":true}

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
  - Punctuation restoration: Breeze-ASR-26 emits Han text with no punctuation, so the raw
    transcript is post-processed through a local LLM (Ollama, default qwen3:14b) that inserts
    ，。！？ etc. WITHOUT changing the wording. Transparent to the engine — the `transcript`
    event just carries punctuated text. Any failure falls back to the raw transcript; a
    word-skeleton check rejects output that changed/translated the wording.

Run:
    gcin-voiced.py                 # real backend (Breeze-ASR-26 via Transformers)
    gcin-voiced.py --device cuda   # force GPU
    gcin-voiced.py --no-punctuate  # skip the LLM punctuation pass
    gcin-voiced.py --mock          # no model/mic; canned transcript (engine tests)
    gcin-voiced.py --list-devices  # show input devices and exit

Requires (real mode): transformers, torch, sounddevice + libportaudio2, numpy.
Punctuation needs a running Ollama with the chosen model (`ollama pull qwen3:14b`); it uses
only stdlib HTTP, no extra Python deps, and degrades gracefully (raw text) if absent.
Mock mode needs none of these — it exists to develop/test the engine side.
"""

import argparse
import json
import os
import re
import socket
import sys
import threading
import time
import urllib.request

MODEL_ID = "MediaTek-Research/Breeze-ASR-26"
TARGET_SR = 16000          # Whisper works at 16 kHz mono
MIN_AUDIO_S = 0.2          # ignore sub-200ms blips (matches the POC)
MOCK_TRANSCRIPT = "語音輸入測試 你好世界"   # canned text for --mock

# Punctuation restoration (Breeze-ASR-26 emits Han text with no punctuation; a small
# local LLM adds ，。！？ without changing the wording. See Punctuator below).
PUNCT_MODEL = "qwen3:14b"                           # Ollama model tag (co-fits the GPU)
PUNCT_URL = "http://localhost:11434/api/chat"      # Ollama chat endpoint
PUNCT_TIMEOUT = 90                                 # seconds (covers a cold GPU model load)
# keep_alive controls how long Ollama keeps the model resident in VRAM. qwen3:14b (~9.8 GB)
# co-fits with Breeze-ASR-26 (~6.6 GB) on a 24 GB GPU (verified: Breeze's .generate() runs with
# ~7.6 GB free), so we keep it resident ("5m") to skip the reload (≈2-3 s/utterance). IMPORTANT:
# a model that does NOT co-reside (e.g. the 13-16 GB vision model qwen2.5vl) will starve
# .generate() and wedge transcription — for those pass `--punctuate-keep-alive 0` so the model
# unloads after each call and the GPU is free for the next transcription.
PUNCT_KEEP_ALIVE = "5m"
PUNCT_NUM_CTX = 1024                               # short utterances; keeps load + KV cheap
PUNCT_SYSTEM = (
    "你是一個標點符號修正助手。使用者會給你一段沒有標點符號的中文語音辨識結果。"
    "請只加上適當的標點符號（，。！？、；：），不要更改、增加或刪除任何文字，"
    "不要翻譯，不要解釋。直接輸出加上標點後的句子。"
)


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


# ── Punctuation restorer ───────────────────────────────────────────────────
#
# Breeze-ASR-26 returns Mandarin Han characters with no punctuation. We post-
# process the raw transcript through a local LLM (Ollama, default qwen3:14b) that
# inserts ，。！？ etc. WITHOUT changing the wording.
#
# This runs inside the transcribe worker thread, so the added latency hides behind
# the engine's "…thinking" glyph and never blocks the key loop. Two safety nets:
#   - Never lose a transcript: any failure (Ollama down, timeout) falls back to the
#     raw, unpunctuated text.
#   - Never corrupt words: accept the LLM output only if its non-punctuation
#     characters (a "word skeleton") are identical to the input. If the model dropped,
#     added, translated, or altered a character, discard its output and keep the raw text.

# Characters ignored when checking "did the wording change": punctuation (ASCII +
# fullwidth) and all whitespace. Everything else must match between input and output.
_PUNCT_CHARS = re.compile(
    r"[\s，。！？、；：「」『』（）《》〈〉【】．·…—－,.!?;:()\[\]{}<>\"'`~@#$%^&*_+=|\\/-]"
)
_THINK_BLOCK = re.compile(r"<think>.*?</think>", re.DOTALL)


def _word_skeleton(s):
    """Strip punctuation + whitespace, leaving only meaningful characters."""
    return _PUNCT_CHARS.sub("", s)


class Punctuator:
    def __init__(self, model=PUNCT_MODEL, url=PUNCT_URL, enabled=True,
                 timeout=PUNCT_TIMEOUT, keep_alive=PUNCT_KEEP_ALIVE,
                 num_ctx=PUNCT_NUM_CTX):
        self.model = model
        self.url = url
        self.enabled = enabled
        self.timeout = timeout
        self.keep_alive = keep_alive
        self.num_ctx = num_ctx

    def _call(self, text):
        """POST one chat request to Ollama; return the assistant content."""
        payload = json.dumps({
            "model": self.model,
            "stream": False,
            "think": False,        # qwen3 et al.: skip reasoning tokens (ignored by others)
            "keep_alive": self.keep_alive,
            "options": {"temperature": 0, "num_ctx": self.num_ctx},
            "messages": [
                {"role": "system", "content": PUNCT_SYSTEM},
                {"role": "user", "content": text},
            ],
        }).encode("utf-8")
        req = urllib.request.Request(
            self.url, data=payload,
            headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=self.timeout) as resp:
            data = json.loads(resp.read().decode("utf-8"))
        return data["message"]["content"].strip()

    @staticmethod
    def _clean(out):
        """Strip reasoning blocks / wrapping quotes / code fences the model might add."""
        out = _THINK_BLOCK.sub("", out).strip()
        if out.startswith("```"):
            out = out.strip("`").strip()
        if len(out) >= 2 and out[0] in "\"'「『“" and out[-1] in "\"'」』”":
            out = out[1:-1].strip()
        return out.splitlines()[0].strip() if out else out

    def add_punct(self, text):
        """Return `text` with punctuation added, or the raw text on any problem."""
        if not self.enabled or not text.strip():
            return text
        try:
            t0 = time.time()
            out = self._clean(self._call(text))
        except Exception as e:           # noqa: BLE001 - never lose a transcript
            log(f"punctuation failed ({e}); using raw transcript")
            return text
        if _word_skeleton(out) != _word_skeleton(text):
            log("punctuator changed the wording; using raw transcript")
            log(f"  raw : {text}")
            log(f"  llm : {out}")
            return text
        log(f"punctuated in {time.time() - t0:.2f}s: {out}")
        return out


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

    def __init__(self, backend, sock_path, punctuator=None):
        self.backend = backend
        self.punctuator = punctuator or Punctuator(enabled=False)
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
            log(f"raw transcript (rtf={rtf}): {text!r}")
            text = self.punctuator.add_punct(text)
            self._send(event="transcript", text=text, alts=alts, rtf=rtf)
        except Exception as e:            # noqa: BLE001
            log(f"transcription error: {e}")
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
        if "punctuate" in msg:
            self.punctuator.enabled = bool(msg["punctuate"])

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
    # Punctuation restoration (post-process ASR output through a local LLM).
    pg = p.add_mutually_exclusive_group()
    pg.add_argument("--punctuate", dest="punctuate", action="store_true",
                    default=None, help="restore punctuation via a local LLM (default: on in real mode)")
    pg.add_argument("--no-punctuate", dest="punctuate", action="store_false",
                    help="disable punctuation restoration")
    p.add_argument("--punctuate-model", default=PUNCT_MODEL,
                   help=f"Ollama model for punctuation (default: {PUNCT_MODEL})")
    p.add_argument("--punctuate-url", default=PUNCT_URL,
                   help="Ollama chat endpoint URL")
    p.add_argument("--punctuate-keep-alive", default=str(PUNCT_KEEP_ALIVE),
                   help="Ollama keep_alive for the LLM (default: '5m' = keep resident, fast; "
                        "qwen3:14b co-fits the ASR model. Pass '0' to unload after each call "
                        "so it never starves the ASR GPU — needed for heavier models)")
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
    # Punctuation defaults on in real mode, off in mock mode (so the dependency-free
    # engine test doesn't need Ollama); --punctuate/--no-punctuate override either way.
    punct_enabled = args.punctuate if args.punctuate is not None else (not args.mock)
    ka = args.punctuate_keep_alive
    keep_alive = int(ka) if ka.lstrip("-").isdigit() else ka     # "0" → 0, "5m" → "5m"
    punctuator = Punctuator(model=args.punctuate_model, url=args.punctuate_url,
                            enabled=punct_enabled, keep_alive=keep_alive)
    if punct_enabled:
        log(f"punctuation restoration on via {punctuator.model} (keep_alive={keep_alive!r})")
    Server(backend, sock_path, punctuator).serve()


if __name__ == "__main__":
    main()
