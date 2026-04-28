PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/zlib/src
BUILDDIR := $(PORTDIR)/build/zlib
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include \
    -DZ_SOLO -DNO_GZIP

SRCS := zlib_port.c

OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libz.a install

$(BUILDDIR)/libz.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libz.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include
	cp $(BUILDDIR)/libz.a $(INSTDIR)/lib/
	cp $(SRCDIR)/zlib.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/zconf.h $(INSTDIR)/include/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
