# SPDX-FileCopyrightText: 2022 Geoffrey D. Bennett <g@b4.vu>
# SPDX-License-Identifier: GPL-3.0-or-later

# Credit to Tom Tromey and Paul D. Smith:
# http://make.mad-scientist.net/papers/advanced-auto-dependency-generation/

VERSION := $(shell \
  git describe --abbrev=4 --dirty --always --tags 2>/dev/null | sed 's/-rc/~rc/g; s/-/./g' || \
  echo $${APP_VERSION:-Unknown} \
)

NAME := scarlett2
SPEC_FILE := $(NAME).spec
TAR_DIR := $(NAME)-$(VERSION)
TAR_FILE := $(TAR_DIR).tar.gz

DEPDIR := .deps
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.d

CFLAGS ?= -ggdb -fno-omit-frame-pointer -fPIE -O2
CFLAGS += -Wall
CFLAGS += -DVERSION=\"$(VERSION)\"

PKG_CONFIG=pkg-config

CFLAGS += $(shell $(PKG_CONFIG) --cflags alsa)

LDFLAGS += $(shell $(PKG_CONFIG) --libs alsa)
LDFLAGS += -lm -lcrypto -pie

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

tar: all
	mkdir -p $(TAR_DIR)
	sed 's_VERSION$$_$(VERSION)_' < $(SPEC_FILE).template > $(TAR_DIR)/$(SPEC_FILE)
	cp -r *.c *.h debian COPYING Makefile $(TAR_DIR)/
	tar czf $(TAR_FILE) $(TAR_DIR)
	rm -rf $(TAR_DIR)

rpm: tar
	rpmbuild -ta $(TAR_FILE)

deb: all
	mkdir -p deb-build/DEBIAN deb-build/usr/bin deb-build/usr/share/doc/$(NAME)
	cp $(TARGET) deb-build/usr/bin/
	cp debian/copyright deb-build/usr/share/doc/$(NAME)/
	sed "s/VERSION/$(VERSION)/g" debian/control > deb-build/DEBIAN/control
	dpkg-deb --root-owner-group --build deb-build $(NAME)_$(VERSION)_$$(dpkg --print-architecture).deb
	rm -rf deb-build

arch:
	sed 's/VERSION/$(VERSION)/g' PKGBUILD.template > PKGBUILD

help:
	@echo "scarlett2"
	@echo
	@echo "This Makefile knows about:"
	@echo "  make"
	@echo "  make install"
	@echo "  make uninstall"
	@echo "  make tar"
	@echo "  make rpm"
	@echo "  make deb"
	@echo "  make arch"

.PHONY: all clean depclean install uninstall tar rpm deb arch help
