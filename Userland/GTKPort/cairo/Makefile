PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/cairo/src
BUILDDIR := $(PORTDIR)/build/cairo
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include -I$(INSTDIR)/include/freetype2 -I$(INSTDIR)/include/glib-2.0 \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := cairo_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libcairo.a install

$(BUILDDIR)/libcairo.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libcairo.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/cairo
	cp $(BUILDDIR)/libcairo.a $(INSTDIR)/lib/
	cp $(SRCDIR)/cairo.h $(INSTDIR)/include/cairo/
	cp $(SRCDIR)/cairo-ft.h $(INSTDIR)/include/cairo/

clean:
	rm -rf $(BUILDDIR)
