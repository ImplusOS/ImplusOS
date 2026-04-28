PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/gdk-pixbuf/src
BUILDDIR := $(PORTDIR)/build/gdk-pixbuf
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include -I$(INSTDIR)/include/glib-2.0 \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := gdk_pixbuf_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libgdk_pixbuf-2.0.a install

$(BUILDDIR)/libgdk_pixbuf-2.0.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libgdk_pixbuf-2.0.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/gdk-pixbuf-2.0
	cp $(BUILDDIR)/libgdk_pixbuf-2.0.a $(INSTDIR)/lib/
	cp $(SRCDIR)/gdk-pixbuf.h $(INSTDIR)/include/gdk-pixbuf-2.0/

clean:
	rm -rf $(BUILDDIR)
