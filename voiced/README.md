# gcin-voiced — voice ASR daemon (Phase A)

The microphone + Breeze-ASR-26 half of the gcin-everywhere voice input method.
The IBus engine stays a thin control client; all the heavy ML runtime lives here,
out of process. See the design: [VOICE-INPUT-DESIGN.md](../../../proj_docs/gcin-everywhere/research/VOICE-INPUT-DESIGN.md).

## Why a separate daemon

Breeze-ASR-26 is ~2 B params (~3 GB safetensors) and wants a CUDA context.
Loading that into `ibus-engine-gcin` would bloat a process that today starts fast
and only loads gcin tables. The daemon loads the model **once, lazily**, owns the
mic, and survives engine focus churn. The engine talks to it over a Unix socket
and never blocks on inference.

## Socket contract

Socket: `$XDG_RUNTIME_DIR/gcin-everywhere/voiced.sock`
Framing: newline-delimited JSON, one object per line, UTF-8.

| engine → daemon | meaning |
|-----------------|---------|
| `{"cmd":"ping"}` | liveness / trigger lazy model load |
| `{"cmd":"start"}` | begin mic capture |
| `{"cmd":"stop"}` | end capture, transcribe, return text |
| `{"cmd":"cancel"}` | drop capture, no transcription |
| `{"cmd":"config","language":"chinese","task":"transcribe"}` | set decode hints |

| daemon → engine | meaning |
|-----------------|---------|
| `{"event":"ready","model":"...","device":"cuda:0"}` | model loaded |
| `{"event":"recording"}` | mic is live (engine shows 🎤) |
| `{"event":"thinking"}` | transcribing (engine shows …) |
| `{"event":"transcript","text":"...","alts":[...],"rtf":0.3}` | result |
| `{"event":"error","msg":"..."}` | failure |

State machine: `idle → recording → thinking → idle` (`cancel` returns to idle
from recording). The backend behind this socket is swappable: Phase A is HF
Transformers (this file); Phase B will be whisper.cpp/GGML, with **zero engine
changes**.

## Run

```bash
# Mock backend — no model, no mic; returns a canned transcript.
# Use this to develop/test the IBus engine side without a GPU or microphone.
python3 gcin-voiced.py --mock

# Real backend (downloads ~3 GB of weights on first run to ~/.cache/huggingface).
python3 gcin-voiced.py                # auto device (CUDA if available)
python3 gcin-voiced.py --device cuda  # force GPU
python3 gcin-voiced.py --list-devices # show audio input devices, then exit
python3 gcin-voiced.py --device-index 4   # pick a specific mic
```

## Test

```bash
python3 test-protocol.py    # drives a full exchange against --mock; no deps
```

## Install (systemd --user)

```bash
mkdir -p ~/.local/lib/gcin-voiced
cp gcin-voiced.py ~/.local/lib/gcin-voiced/
python3 -m venv ~/.local/lib/gcin-voiced/venv
~/.local/lib/gcin-voiced/venv/bin/pip install -r requirements.txt

mkdir -p ~/.config/systemd/user
cp gcin-voiced.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now gcin-voiced.service
```

The service starts the daemon at login but **defers model loading** until the
first `ping`/`start`, so non-voice sessions pay only the idle socket cost.

## Requires

Real mode: `transformers`, `torch`, `numpy`, `sounddevice` (+ `sudo apt install
libportaudio2`), `librosa`, `soundfile`, `accelerate` — see `requirements.txt`.
Mock mode needs none of these.
