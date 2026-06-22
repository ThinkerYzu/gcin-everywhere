# gcin-everywhere

gcin-everywhere brings [gcin](https://github.com/pkg-ime/gcin)'s Traditional Chinese
input methods to modern GNOME/Wayland desktops via IBus. It wraps gcin's battle-tested
input engines — preserving the exact key sequences and character tables that long-time
gcin users rely on — without rewriting any input logic.

gcin was the standard Traditional Chinese IME on Linux for many years. When GNOME moved
to Wayland, gcin stopped working. This project fills that gap.

**Status:** Six input methods plus a unified switcher, all working on GNOME/Wayland —
Cangjie (倉頡), CJ5 (倉頡五代), Zhuyin (注音/Bopomofo), Quick (速成), Array (行列),
Simplex+Punctuation (標點簡易), and **gcin-everywhere**, a single engine that switches
between them in place via `Ctrl+Alt+<digit>` and toggles English with `Ctrl+Space` —
just like classic gcin.

---

## How it works

gcin's input engine code (`gtab.cpp`, `pho.cpp`, and related files) is compiled into a
static library (`libgcin-core.a`) with all X11/GTK dependencies stubbed out. An IBus
engine wrapper (`gcin_engine.c`) links against this library and presents the engines to
GNOME via IBus's standard D-Bus interface.

```
GNOME / Wayland
      ↓
 ibus-daemon
      ↓
 ibus-engine-gcin          (gcin_engine.c — IBus GObject wrapper)
      ↓
 libgcin-core.a            (gcin source compiled with GTK/X11 stubbed)
      ↓
 ~/.local/share/gcin/      (cj.gtab, pho.tab2, zo.kbm, tsin32, …)
```

---

## Requirements

- GCC and make
- GLib 2 (`libglib2.0-dev`)
- IBus 1.5 headers — either `libibus-1.0-dev` or extracted from the `.deb`:
  ```bash
  apt-get download libibus-1.0-dev
  dpkg-deb -x libibus-1.0-dev_*.deb /tmp/ibus-dev-extract
  ```
- IBus 1.5 runtime (`libibus-1.0.so.5` — usually already installed with GNOME)

---

## Build and Install

### 1. Clone with submodule

```bash
git clone --recurse-submodules https://github.com/ThinkerYzu/gcin-everywhere.git
cd gcin-everywhere
```

### 2. Build, test, and install

```bash
make test     # builds everything, compiles data tables, runs unit tests
make install  # installs to ~/.local/ (no root required)
```

That's it. The top-level `Makefile` handles the full pipeline:
builds `libgcin-core.a`, compiles the data tables, runs unit tests,
builds `ibus-engine-gcin`, and installs.

`make install` prompts for **sudo once** — the IBus component XML must go in the system
directory (see note below). Everything else stays user-local.

Installed files:

| File | Destination |
|------|-------------|
| `ibus-engine-gcin` | `~/.local/lib/ibus-gcin/` |
| Data tables | `~/.local/share/gcin/` |
| Component XML | `/usr/share/ibus/component/gcin.xml` (system — needs sudo) |
| Systemd service | `~/.config/systemd/user/ibus-engine-gcin.service` |
| GNOME Shell extension | `~/.local/share/gnome-shell/extensions/gcin-everywhere@gcin.dev/` (GNOME only) |

The systemd service is enabled and started automatically. The engine starts at login
and restarts on failure.

> **Note:** ibus-daemon only scans the **system** component directories
> (`/usr/share/ibus/component`), not `~/.local/share/ibus/component`, so the component XML
> must be installed there (hence the sudo step) or the engines never appear in
> `ibus list-engine` / the GNOME picker. The engine *binary* itself stays user-local and
> is launched by the systemd service (GNOME does not auto-spawn user-local engines). The
> component dir is overridable: `make install COMPDIR=/some/dir`.

---

## Enable in GNOME

1. Open **Settings → Keyboard → Input Sources**
2. Click **+** → search "Chinese (Traditional)"
3. Add the engines you want. For the all-in-one experience, add **gcin Everywhere
   (全能切換)** — it covers every method via hotkeys. Or add any of the single-method
   engines: **gcin Cangjie (倉頡)**, **gcin Cangjie 5 (倉頡五代)**, **gcin Zhuyin (注音)**,
   **gcin Quick (速成)**, **gcin Array (行列)**, **gcin Simplex+Punct (標點簡易)**.
4. Switch between input sources with **Super+Space**.

### GNOME panel indicator (recommended for gcin Everywhere)

On GNOME, `make install` also installs a small GNOME Shell extension that shows the
**active method's glyph** in the top bar (倉/五/注/速/標/列, or 英 in English) so you can always
tell which method gcin Everywhere is in. The indicator is shown **only** while the gcin
Everywhere source is active. (Install is gated on detecting `gnome-shell`; on other
desktops it's skipped — the IBus property drives their panels instead. Force it anywhere
with `make install-extension FORCE_EXTENSION=1`.)

`make install` also **enables** it for you (it adds the UUID to GNOME's
`enabled-extensions`), so there's no separate `gnome-extensions enable` step. The one thing
you must do on **Wayland** is **log out and back in** after the first install — GNOME Shell
loads newly-installed extensions only at login (the shell can't be reloaded live on
Wayland). After that the indicator updates instantly as you switch with `Ctrl+Alt+<digit>` /
`Ctrl+Space`.

**How it works:** the engine publishes the current method to a tiny state file
(`$XDG_RUNTIME_DIR/gcin-everywhere/state`); the extension watches it with a file monitor
(inotify — no polling) and mirrors the glyph in the panel. This is needed because GNOME
Shell ignores IBus property symbol updates (see the Usage note above). On desktops with no
extension the file is simply ignored — the IBus property still drives KDE / the standalone
IBus panel.

---

## Usage

**Cangjie (倉頡):** Type the component keys, press Space, then a number to select.

| Keys | Result |
|------|--------|
| `k` `o` `Space` `1` | 大人 |
| `a` `b` `Space` `1` | 明 |

**Zhuyin (注音/Bopomofo):** Type the phonetic keys (Daqian/大千 standard layout), then
a tone key. Candidates appear automatically.

| Keys | Result |
|------|--------|
| `j` `u` `4` `1` | 住 (or other ㄓㄨˋ character) |
| `m` `4` `1` | 妹 (or other ㄇˋ character) |

### gcin Everywhere (全能切換) — switch methods without leaving the engine

With the **gcin Everywhere** source active, switch input method in place with
`Ctrl+Alt+<digit>` (mirrors classic gcin's hotkeys), and toggle English with
`Ctrl+Space`:

| Hotkey | Method |
|--------|--------|
| `Ctrl+Alt+1` | 倉頡 Cangjie |
| `Ctrl+Alt+2` | 倉五 CJ5 |
| `Ctrl+Alt+3` | 注音 Zhuyin |
| `Ctrl+Alt+4` | 速成 Quick |
| `Ctrl+Alt+5` | 標點簡易 SimplexPunc |
| `Ctrl+Alt+8` | 行列 Array |
| `Ctrl+Space` | Toggle Chinese ↔ English (resumes the last method) |

**Starts in English on every focus.** When gcin Everywhere is active, each newly-focused text
field starts in **English** — switch to another window (or click into a different field) and
you're in English passthrough, not whatever Chinese method you were last using. Your selected
method is remembered, so `Ctrl+Space` (or `Ctrl+Alt+<digit>`) resumes it. This is deliberate:
IBus tells the engine *that* focus changed but not *which* window got it, so the reset applies
to any focus change — the familiar per-field behavior of classic IMEs.

**Seeing the active method (GNOME panel indicator).** Because gcin-everywhere is a
*single* engine, GNOME Shell's built-in input indicator only shows its fixed component
symbol (全) and can't tell you which method is active — GNOME ignores the live
`IBusProperty` symbol updates the engine sends (those *do* work on KDE / the standalone
IBus panel, showing 倉/五/注/速/標/列 or 英). To get a live indicator on GNOME, install the
bundled GNOME Shell extension (see below); it shows the active method's glyph in the top
bar and appears **only** while gcin Everywhere is the active source.

> **Important — free up `Ctrl+Space`:** GNOME/mutter intercepts keyboard shortcuts before
> the IBus engine, so the `Ctrl+Space` English toggle only works if no desktop shortcut
> binds plain `Ctrl+Space`. Move them off it once:
> ```bash
> gsettings set org.gnome.desktop.wm.keybindings switch-input-source "['<Shift><Control>space']"
> gsettings set org.gnome.desktop.wm.keybindings switch-input-source-backward "[]"
> gsettings set org.freedesktop.ibus.general.hotkey trigger \
>   "['Zenkaku_Hankaku', 'Alt+Kanji', 'Alt+grave', 'Hangul', 'Alt+Release+Alt_R']"
> ```
> `wm.keybindings` apply immediately; the IBus `trigger` change takes effect after you log
> out and back in. (A symmetric "two presses to switch" is the sign `Ctrl+Space` is still
> double-bound.)

---

## Rebuilding after changes

```bash
make install
systemctl --user restart ibus-engine-gcin
```

---

## Repository layout

```
gcin/               gcin upstream source (git submodule: ThinkerYzu/gcin)
gcin-core/          libgcin-core.a — gcin engine with X11/GTK stubbed out
  gcin-core.h       public API
  gcin_stubs.cpp    stub implementations + public API entry points
  test_feedkey.c    unit tests
ibus-engine/        IBus wrapper
  gcin_engine.c     IBus GObject engine
  component/        IBus component XML
  Makefile          build + install
gnome-extension/    GNOME Shell extension (top-bar method indicator)
  gcin-everywhere@gcin.dev/
    extension.js    watches the engine's state file, mirrors the glyph
    metadata.json   shell-version + uuid
    stylesheet.css  panel label styling
```

---

## Future work

- **Windows** via Text Services Framework (TSF)
- **macOS** via Input Method Kit (IMKit)
- **More methods:** Dayi (大易), Buxiemi (嘸蝦米) — pending source tables
- **Packaging:** `.deb` / `.rpm` package for easier installation

Done: Cangjie, CJ5, Zhuyin, Quick, Array, Simplex+Punctuation, the unified
gcin-everywhere switcher (`Ctrl+Alt+<digit>` + `Ctrl+Space` English toggle), and a GNOME
Shell extension showing the active method in the top panel.
