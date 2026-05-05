GCIN_CORE := ../gcin-core

# Override IBUS_CFLAGS/IBUS_LIBS if libibus-1.0-dev is not system-installed:
#   make IBUS_CFLAGS="-I/tmp/ibus-dev-extract/usr/include/ibus-1.0 ..."
IBUS_CFLAGS ?= $(shell pkg-config --cflags ibus-1.0)
IBUS_LIBS   ?= $(shell pkg-config --libs   ibus-1.0)

CFLAGS := -g -O2 $(IBUS_CFLAGS) -I$(GCIN_CORE)
LDFLAGS := $(IBUS_LIBS) -L$(GCIN_CORE) -lgcin-core -lm

.PHONY: all clean

all: ibus-engine-gcin

ibus-engine-gcin: gcin_engine.c $(GCIN_CORE)/libgcin-core.a
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(GCIN_CORE)/libgcin-core.a:
	$(MAKE) -C $(GCIN_CORE)

clean:
	rm -f ibus-engine-gcin
