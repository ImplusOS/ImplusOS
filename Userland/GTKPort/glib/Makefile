PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/glib/src
BUILDDIR := $(PORTDIR)/build/glib
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include \
    -D_GNU_SOURCE

SRCS := glib_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libglib-2.0.a install

$(BUILDDIR)/libglib-2.0.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libglib-2.0.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/glib-2.0
	cp $(BUILDDIR)/libglib-2.0.a $(INSTDIR)/lib/
	cp $(SRCDIR)/glib.h $(INSTDIR)/include/glib-2.0/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
