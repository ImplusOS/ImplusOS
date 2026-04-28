PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/libevdev
BUILDDIR := $(PORTDIR)/build/libevdev
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(SRCDIR)/include \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := libevdev/libevdev.c libevdev/libevdev-names.c libevdev/libevdev-uinput.c

OBJS := $(addprefix $(BUILDDIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all install clean

all: $(BUILDDIR)/libevdev.a install

$(BUILDDIR)/libevdev.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/libevdev/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libevdev.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/libevdev-1.0/libevdev
	cp $(BUILDDIR)/libevdev.a $(INSTDIR)/lib/
	cp $(SRCDIR)/include/libevdev/libevdev.h $(INSTDIR)/include/libevdev-1.0/libevdev/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
