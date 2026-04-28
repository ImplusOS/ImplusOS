PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/dbus/src
BUILDDIR := $(PORTDIR)/build/dbus
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := dbus_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libdbus-1.a install

$(BUILDDIR)/libdbus-1.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libdbus-1.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/dbus-1.0
	cp $(BUILDDIR)/libdbus-1.a $(INSTDIR)/lib/
	cp $(SRCDIR)/dbus.h $(INSTDIR)/include/dbus-1.0/

clean:
	rm -rf $(BUILDDIR)
