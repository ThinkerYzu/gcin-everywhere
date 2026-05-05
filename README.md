# gcin-everywhere

Port of gcin's Traditional Chinese input engine to modern platforms, starting with GNOME/Wayland via IBus.

## Repository Structure

```
gcin/           gcin upstream source (git submodule: github.com/pkg-ime/gcin)
ibus-engine/    IBus engine wrapper — new code that integrates gcin core with IBus
```

## Project Documentation

Design, spec, and implementation guide live in the project docs:
`proj_docs/gcin-everywhere/` (in the claudebugzilla repo)

## Phase 1 Goal

Wrap gcin's Cangjie (倉頡) and Zhuyin (注音) input engines as an IBus engine
so they work on GNOME/Wayland desktops.

## Architecture

```
GNOME apps → ibus-daemon → ibus-engine-gcin
                                ↓
                         ibus-engine/gcin_engine.c   (IBus GObject)
                                ↓
                         ibus-engine/gcin_adapter.c  (X11 stub shim)
                                ↓
                    gcin/gtab.cpp | gcin/pho.cpp     (gcin core, compiled in)
                                ↓
                    gcin/data/cj.gtab | pho.tab      (data tables)
```

## Build

See `proj_docs/gcin-everywhere/IMPLEMENTATION-GUIDE.md` for prerequisites and build steps.
(To be filled when IMPLEMENTATION-GUIDE.md is drafted.)
