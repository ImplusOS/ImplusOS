PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/libpng/src
BUILDDIR := $(PORTDIR)/build/libpng
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := libpng_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libpng.a install

$(BUILDDIR)/libpng.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libpng.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include
	cp $(BUILDDIR)/libpng.a $(INSTDIR)/lib/
	cp $(SRCDIR)/png.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/pngconf.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/pnglibconf.h $(INSTDIR)/include/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
