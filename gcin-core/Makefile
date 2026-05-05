GCIN     := ../gcin

CFLAGS := -x c -std=gnu99 -g -O2 -Wno-implicit-function-declaration \
           -DGCIN_CORE_BUILD -DHAVE_CONFIG_H -DUSE_TSIN=1 \
           -DGCIN_TABLE_DIR=\"/usr/share/gcin\" \
           -DGCIN_BIN_DIR=\"/usr/lib/gcin\" \
           -I$(GCIN) -I$(GCIN)/IMdkit/include

GCIN_SRCS := \
    $(GCIN)/gtab.cpp          \
    $(GCIN)/gtab-init.cpp     \
    $(GCIN)/gtab-list.cpp     \
    $(GCIN)/gtab-buf.cpp      \
    $(GCIN)/gtab-util.cpp     \
    $(GCIN)/gtab-tsin-fname.cpp \
    $(GCIN)/pho.cpp           \
    $(GCIN)/pho-lookup.cpp    \
    $(GCIN)/pho-util.cpp      \
    $(GCIN)/pho-kbm-name.cpp  \
    $(GCIN)/pho-sym.cpp       \
    $(GCIN)/tsin.cpp          \
    $(GCIN)/tsin-util.cpp     \
    $(GCIN)/tsin-char.cpp     \
    $(GCIN)/tsin-scan.cpp     \
    $(GCIN)/tsin-parse.cpp    \
    $(GCIN)/util.cpp          \
    $(GCIN)/gcin-conf.cpp     \
    $(GCIN)/gcin-settings.cpp \
    $(GCIN)/fullchar.cpp      \
    $(GCIN)/cache.cpp         \
    $(GCIN)/lang.cpp          \
    $(GCIN)/unix-exec.cpp     \
    $(GCIN)/locale.cpp        \
    $(GCIN)/phrase.cpp        \
    $(GCIN)/gtab-use-count.cpp \
    $(GCIN)/table-update.cpp

CORE_SRCS := gcin_stubs.cpp

OBJS := $(patsubst %.cpp,%.o,$(notdir $(GCIN_SRCS))) \
        $(patsubst %.cpp,%.o,$(CORE_SRCS))

.PHONY: all clean

all: libgcin-core.a

libgcin-core.a: $(OBJS)
	$(AR) rcs $@ $^

vpath %.cpp $(GCIN) .

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o libgcin-core.a
