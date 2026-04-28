PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/pixman
BUILDDIR := $(PORTDIR)/build/pixman
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR)/pixman \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include \
    -DPACKAGE=\"pixman\" -DPACKAGE_VERSION=\"0.42.2\" \
    -DHAVE_PTHREADS=0 -DPIXMAN_NO_TLS=1 -DUSE_SSE2=0 -DUSE_SSSE3=0 \
    -DUSE_MMX=0 -DUSE_VMX=0 -DUSE_ARM_SIMD=0 -DUSE_ARM_NEON=0

SRCS := pixman.c pixman-access.c pixman-access-accessors.c \
    pixman-bits-image.c pixman-combine32.c pixman-combine-float.c \
    pixman-conical-gradient.c pixman-edge.c pixman-edge-accessors.c \
    pixman-fast-path.c pixman-filter.c pixman-general.c \
    pixman-glyph.c pixman-gradient-walker.c pixman-image.c \
    pixman-implementation.c pixman-linear-gradient.c pixman-matrix.c \
    pixman-noop.c pixman-radial-gradient.c pixman-region16.c \
    pixman-region32.c pixman-solid-fill.c pixman-trap.c pixman-utils.c

OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libpixman-1.a install

$(BUILDDIR)/libpixman-1.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/pixman/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libpixman-1.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/pixman-1
	cp $(BUILDDIR)/libpixman-1.a $(INSTDIR)/lib/
	cp $(SRCDIR)/pixman/pixman.h $(INSTDIR)/include/pixman-1/
	cp $(SRCDIR)/pixman/pixman-version.h $(INSTDIR)/include/pixman-1/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
