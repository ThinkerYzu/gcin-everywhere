GCIN  := gcin
CORE  := gcin-core
TABLES ?= $(CURDIR)/tables

CORE_CFLAGS := \
    -x c -std=gnu99 -O2 -Wno-implicit-function-declaration \
    -DGCIN_CORE_BUILD -DHAVE_CONFIG_H -DUSE_TSIN=1 \
    -DGCIN_TABLE_DIR=\"/usr/share/gcin\" \
    -DGCIN_BIN_DIR=\"/usr/lib/gcin\" \
    -I$(GCIN) -I$(GCIN)/IMdkit/include -I$(CORE)

TOOL_LINK := -L$(CORE) -lgcin-core -lm

GTK_STUB := /tmp/gcin-everywhere-gtk-stub.c

EXT_UUID := gcin-everywhere@gcin.dev
EXT_SRC  := gnome-extension/$(EXT_UUID)
EXT_DEST := $(HOME)/.local/share/gnome-shell/extensions/$(EXT_UUID)

.PHONY: all core engine table-tools tables test install install-extension clean

all: core engine

core:
	$(MAKE) -C $(CORE)

engine: core
	$(MAKE) -C ibus-engine

# ── Data table compilation ────────────────────────────────────────────

$(GTK_STUB):
	printf '%s\n' \
	    'void gtk_init(int*a,char***b){}' \
	    'typedef void Display;' \
	    'void send_gcin_message(Display*d,char*s){}' \
	    'typedef unsigned short phokey_t;' \
	    'phokey_t pinyin2phokey(char*s){return 0;}' > $@

table-tools: core $(GTK_STUB)
	$(CC) $(CORE_CFLAGS) $(GCIN)/gcin2tab.cpp $(GTK_STUB) -o $(GCIN)/gcin2tab $(TOOL_LINK)
	$(CC) $(CORE_CFLAGS) $(GCIN)/phoa2d.cpp   $(GTK_STUB) -o $(GCIN)/phoa2d   $(TOOL_LINK)
	$(CC) $(CORE_CFLAGS) $(GCIN)/tsa2d32.cpp  $(GTK_STUB) -o $(GCIN)/tsa2d32  $(TOOL_LINK)
	$(CC) $(CORE_CFLAGS) $(GCIN)/kbmcv.cpp    $(GTK_STUB) -o $(GCIN)/kbmcv    $(TOOL_LINK)

tables: table-tools
	mkdir -p $(TABLES)
	cd $(GCIN) && NO_GTK_INIT=1 ./gcin2tab  data/cj.cin
	cp $(GCIN)/data/cj.gtab  $(TABLES)/
	cd $(GCIN) && NO_GTK_INIT=1 ./gcin2tab  data/simplex.cin
	cp $(GCIN)/data/simplex.gtab $(TABLES)/
	cd $(GCIN) && NO_GTK_INIT=1 ./gcin2tab  data/ar30.cin
	cp $(GCIN)/data/ar30.gtab $(TABLES)/
	cd $(GCIN) && NO_GTK_INIT=1 ./gcin2tab  data/cj5.cin
	cp $(GCIN)/data/cj5.gtab $(TABLES)/
	cd $(GCIN) && NO_GTK_INIT=1 ./gcin2tab  data/simplex-punc.cin
	cp $(GCIN)/data/simplex-punc.gtab $(TABLES)/
	cd $(GCIN) && NO_GTK_INIT=1 ./phoa2d    data/pho.tab2.src
	cp $(GCIN)/data/pho.tab2 $(TABLES)/
	cd $(GCIN) && NO_GTK_INIT=1 ./tsa2d32   data/tsin.src $(TABLES)/tsin32
	cd $(GCIN) && NO_GTK_INIT=1 ./kbmcv     data/zo.kbmsrc
	cp $(GCIN)/data/zo.kbm   $(TABLES)/
	cp $(GCIN)/data/gtab.list        $(TABLES)/
	cp $(GCIN)/data/phrase.table     $(TABLES)/
	cp $(GCIN)/data/phrase-ctrl.table $(TABLES)/

# ── Test and install ─────────────────────────────────────────────────

test: core tables
	$(MAKE) -C $(CORE) test GCIN_TABLE_DIR=$(TABLES)

install: engine tables install-extension
	$(MAKE) -C ibus-engine install TABLES=$(TABLES)

# GNOME Shell indicator extension — shows the active method in the top panel.
# Installed only on GNOME (detected via the gnome-shell binary); skipped elsewhere,
# where the IBus property already drives the panel. User-local (no sudo).
# Also auto-enables by adding the UUID to GNOME's enabled-extensions list — this
# works even before the shell has scanned the new extension (unlike
# `gnome-extensions enable`, which would error). The user only needs to log out/in
# on Wayland for the shell to load it. Force install with FORCE_EXTENSION=1.
install-extension:
	@if [ -n "$(FORCE_EXTENSION)" ] || command -v gnome-shell >/dev/null 2>&1; then \
	    mkdir -p $(EXT_DEST); \
	    cp -r $(EXT_SRC)/* $(EXT_DEST)/; \
	    echo "Installed GNOME extension to $(EXT_DEST)."; \
	    if command -v gsettings >/dev/null 2>&1; then \
	        EN=$$(gsettings get org.gnome.shell enabled-extensions 2>/dev/null || echo "@as []"); \
	        case "$$EN" in \
	            *"'$(EXT_UUID)'"*) echo "Already enabled in GNOME." ;; \
	            "@as []"|"[]") gsettings set org.gnome.shell enabled-extensions "['$(EXT_UUID)']" && echo "Enabled in GNOME." ;; \
	            *) gsettings set org.gnome.shell enabled-extensions "$${EN%]}, '$(EXT_UUID)']" && echo "Enabled in GNOME." ;; \
	        esac; \
	        echo ">>> Log out and back in for the indicator to appear (Wayland loads new extensions only at login)."; \
	    else \
	        echo "Enable it: gnome-extensions enable $(EXT_UUID)  (log out/in on Wayland)."; \
	    fi; \
	else \
	    echo "No GNOME Shell detected — skipping the panel-indicator extension."; \
	    echo "(Force it with: make install-extension FORCE_EXTENSION=1)"; \
	fi

# ── Clean ────────────────────────────────────────────────────────────

clean:
	$(MAKE) -C $(CORE) clean
	$(MAKE) -C ibus-engine clean
	rm -f $(GCIN)/gcin2tab $(GCIN)/phoa2d $(GCIN)/tsa2d32 $(GCIN)/kbmcv
	rm -rf $(TABLES) $(GTK_STUB)
