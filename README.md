# gcin-everywhere

gcin-everywhere brings [gcin](https://github.com/pkg-ime/gcin)'s Traditional Chinese
input methods to modern GNOME/Wayland desktops via IBus. It wraps gcin's battle-tested
Cangjie (倉頡) and Zhuyin (注音/Bopomofo) engines — preserving the exact key sequences
and character tables that long-time gcin users rely on — without rewriting any input
logic.

gcin was the standard Traditional Chinese IME on Linux for many years. When GNOME moved
to Wayland, gcin stopped working. This project fills that gap.

**Status:** Phase 1 complete — Cangjie and Zhuyin work on GNOME/Wayland.

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

### 2. Build data tables

gcin's character and phonetic tables must be compiled from source. Build the table
tools and run them:

```bash
cd gcin-core && make && cd ..

cd gcin
# Build table tools (no GTK2 required)
CFLAGS="-x c -std=gnu99 -O2 -Wno-implicit-function-declaration \
  -DGCIN_CORE_BUILD -DHAVE_CONFIG_H -DUSE_TSIN=1 \
  -DGCIN_TABLE_DIR=\"/usr/share/gcin\" -DGCIN_BIN_DIR=\"/usr/lib/gcin\" \
  -I. -I./IMdkit/include -I../gcin-core"
printf 'void gtk_init(int*a,char***b){}\ntypedef void Display;\nvoid send_gcin_message(Display*d,char*s){}\ntypedef unsigned short phokey_t;\nphokey_t pinyin2phokey(char*s){return 0;}\n' > /tmp/gtk_stub.c
cc $CFLAGS gcin2tab.cpp /tmp/gtk_stub.c -o gcin2tab -L../gcin-core -lgcin-core -lm
cc $CFLAGS phoa2d.cpp   /tmp/gtk_stub.c -o phoa2d   -L../gcin-core -lgcin-core -lm
cc $CFLAGS tsa2d32.cpp  /tmp/gtk_stub.c -o tsa2d32  -L../gcin-core -lgcin-core -lm
cc $CFLAGS kbmcv.cpp    /tmp/gtk_stub.c -o kbmcv    -L../gcin-core -lgcin-core -lm

# Compile tables
mkdir -p /tmp/gcin-tables
NO_GTK_INIT=1 ./gcin2tab data/cj.cin      && cp data/cj.gtab  /tmp/gcin-tables/
NO_GTK_INIT=1 ./phoa2d   data/pho.tab2.src && cp data/pho.tab2 /tmp/gcin-tables/
NO_GTK_INIT=1 ./tsa2d32  data/tsin.src /tmp/gcin-tables/tsin32
NO_GTK_INIT=1 ./kbmcv    data/zo.kbmsrc    && cp data/zo.kbm   /tmp/gcin-tables/
cp data/gtab.list /tmp/gcin-tables/
cd ..
```

### 3. Run unit tests

```bash
cd gcin-core && GCIN_TABLE_DIR=/tmp/gcin-tables make test && cd ..
# expected: 9 passed, 0 failed
```

### 4. Install

```bash
cd ibus-engine
make install TABLES=/tmp/gcin-tables
```

This installs everything to `~/.local/` (no root required):

| File | Destination |
|------|-------------|
| `ibus-engine-gcin` | `~/.local/lib/ibus-gcin/` |
| Data tables | `~/.local/share/gcin/` |
| Component XML | `~/.local/share/ibus/component/gcin.xml` |
| Systemd service | `~/.config/systemd/user/ibus-engine-gcin.service` |

The systemd service is enabled and started automatically. The engine starts at login
and restarts on failure.

> **Note:** GNOME does not auto-spawn user-local IBus engines on demand, so the systemd
> service is required. System-wide installation (`sudo make install PREFIX=/usr`) would
> allow GNOME to auto-spawn the engine without a service, but is not necessary.

---

## Enable in GNOME

1. Open **Settings → Keyboard → Input Sources**
2. Click **+** → search "Chinese (Traditional)"
3. Add **gcin Cangjie (倉頡)** and/or **gcin Zhuyin (注音)**
4. Switch engines with **Super+Space**

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

---

## Rebuilding after changes

```bash
cd ibus-engine
make install TABLES=/tmp/gcin-tables
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
```

---

## Future work

- **Phase 2:** Additional input methods — Quick (速成), Array (行列), Dayi (大易)
- **Phase 3:** Windows via Text Services Framework (TSF)
- **Phase 3:** macOS via Input Method Kit (IMKit)
- **Packaging:** `.deb` / `.rpm` package for easier installation
