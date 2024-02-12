# SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

# Credit to Tom Tromey and Paul D. Smith:
# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/

VERSION := $(shell \
  git describe --abbrev=4 --dirty --always --tags 2>/dev/null || \
  echo $${APP_VERSION:-Unknown} \
)

DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

CFLAGS := -Wall -Werror -ggdb -fno-omit-frame-pointer -O2 -D_FORTIFY_SOURCE=2
CFLAGS += -DVERSION=\"$(VERSION)\"

PKG_CONFIG=pkg-config

CFLAGS += $(shell $(PKG_CONFIG) --cflags alsa)

LDFLAGS += $(shell $(PKG_CONFIG) --libs alsa)
LDFLAGS += -lm -lcrypto

COMPILE.c = $(CC) $(DEPFLAGS) $(CFLAGS) -c

%.o: %.c
%.o: %.c Makefile $(DEPDIR)/%.d | $(DEPDIR)
	$(COMPILE.c) $(OUTPUT_OPTION) $<

SRCS := $(sort $(wildcard *.c))
OBJS := $(patsubst %.c,%.o,$(SRCS))
TARGET := scarlett2

all: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

depclean:
	rm -rf $(DEPDIR)

$(DEPDIR): ; @mkdir -p $@

DEPFILES := $(SRCS:%.c=$(DEPDIR)/%.d)
$(DEPFILES):

include $(wildcard $(DEPFILES))

$(TARGET): $(OBJS)
	cc -o $(TARGET) $(OBJS) ${LDFLAGS}

ifeq ($(PREFIX),)
  PREFIX := /usr/local
endif

BINDIR := $(DESTDIR)$(PREFIX)/bin

install: all
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)

uninstall:
	rm -f $(BINDIR)/$(TARGET)

help:
	@echo "scarlett2"
	@echo
	@echo "This Makefile knows about:"
	@echo "  make"
	@echo "  make install"
	@echo "  make uninstall"
