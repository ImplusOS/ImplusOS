PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/gtk3/src
BUILDDIR := $(PORTDIR)/build/gtk3
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include \
    -I$(INSTDIR)/include/glib-2.0 \
    -I$(INSTDIR)/include/cairo \
    -I$(INSTDIR)/include/pango-1.0 \
    -I$(INSTDIR)/include/gdk-pixbuf-2.0 \
    -I$(INSTDIR)/include/atk-1.0 \
    -I$(INSTDIR)/include/epoxy \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := gdk_port.c gtk_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libgtk-3.a install

$(BUILDDIR)/libgtk-3.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libgtk-3.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/gtk-3.0/gtk $(INSTDIR)/include/gtk-3.0/gdk
	cp $(BUILDDIR)/libgtk-3.a $(INSTDIR)/lib/
	cp $(SRCDIR)/gtk/gtk.h $(INSTDIR)/include/gtk-3.0/gtk/
	cp $(SRCDIR)/gdk/gdk.h $(INSTDIR)/include/gtk-3.0/gdk/

clean:
	rm -rf $(BUILDDIR)
