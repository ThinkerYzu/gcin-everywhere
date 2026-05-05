GCIN_CORE := ../gcin-core

IBUS_EXTRACT := /tmp/ibus-dev-extract/usr/include/ibus-1.0
GLIB_CFLAGS  := $(shell pkg-config --cflags glib-2.0 gobject-2.0 gio-2.0)
GLIB_LIBS    := $(shell pkg-config --libs   glib-2.0 gobject-2.0 gio-2.0)

# Use system pkg-config if available; fall back to extracted dev headers
ifeq ($(shell pkg-config --exists ibus-1.0 && echo yes),yes)
  IBUS_CFLAGS ?= $(shell pkg-config --cflags ibus-1.0)
  IBUS_LIBS   ?= $(shell pkg-config --libs   ibus-1.0)
else
  IBUS_CFLAGS ?= -I$(IBUS_EXTRACT) $(GLIB_CFLAGS)
  IBUS_LIBS   ?= /usr/lib/x86_64-linux-gnu/libibus-1.0.so.5 $(GLIB_LIBS)
endif

CFLAGS  := -g -O2 $(IBUS_CFLAGS) -I$(GCIN_CORE)
LDFLAGS := $(IBUS_LIBS) -L$(GCIN_CORE) -lgcin-core -lm

PREFIX  ?= $(HOME)/.local
BINDIR   = $(PREFIX)/lib/ibus-gcin
DATADIR  = $(PREFIX)/share/gcin
COMPDIR  = $(PREFIX)/share/ibus/component
TABLES   ?= /tmp/gcin-tables
SYSTEMD  ?= $(HOME)/.config/systemd/user

.PHONY: all clean install

all: ibus-engine-gcin

ibus-engine-gcin: gcin_engine.c $(GCIN_CORE)/libgcin-core.a
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(GCIN_CORE)/libgcin-core.a:
	$(MAKE) -C $(GCIN_CORE)

install: all
	mkdir -p $(BINDIR) $(DATADIR) $(COMPDIR) $(SYSTEMD)
	install -m 755 ibus-engine-gcin $(BINDIR)/ibus-engine-gcin
	cp $(TABLES)/* $(DATADIR)/
	sed 's|/usr/lib/ibus-gcin/ibus-engine-gcin|$(BINDIR)/ibus-engine-gcin|' \
	    component/gcin.xml > $(COMPDIR)/gcin.xml
	sed 's|ExecStart=.*|ExecStart=$(BINDIR)/ibus-engine-gcin|' \
	    ibus-engine-gcin.service > $(SYSTEMD)/ibus-engine-gcin.service
	systemctl --user enable --now ibus-engine-gcin.service
	@echo "Installed to $(PREFIX)."

clean:
	rm -f ibus-engine-gcin
