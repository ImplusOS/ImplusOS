PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/libdrm
BUILDDIR := $(PORTDIR)/build/libdrm
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(SRCDIR)/include/drm \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include \
    -DHAVE_VISIBILITY=0 -DMAJOR_IN_SYSMACROS=0

SRCS := xf86drm.c xf86drmMode.c xf86drmHash.c xf86drmRandom.c xf86drmSL.c

OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libdrm.a install

$(BUILDDIR)/libdrm.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libdrm.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/libdrm
	cp $(BUILDDIR)/libdrm.a $(INSTDIR)/lib/
	cp $(SRCDIR)/include/drm/*.h $(INSTDIR)/include/libdrm/ 2>/dev/null || true
	cp $(SRCDIR)/xf86drm.h $(INSTDIR)/include/libdrm/ 2>/dev/null || true
	cp $(SRCDIR)/xf86drmMode.h $(INSTDIR)/include/libdrm/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
