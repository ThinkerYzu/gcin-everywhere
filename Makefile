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

.PHONY: all core engine table-tools tables test install clean

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

install: engine tables
	$(MAKE) -C ibus-engine install TABLES=$(TABLES)

# ── Clean ────────────────────────────────────────────────────────────

clean:
	$(MAKE) -C $(CORE) clean
	$(MAKE) -C ibus-engine clean
	rm -f $(GCIN)/gcin2tab $(GCIN)/phoa2d $(GCIN)/tsa2d32 $(GCIN)/kbmcv
	rm -rf $(TABLES) $(GTK_STUB)
