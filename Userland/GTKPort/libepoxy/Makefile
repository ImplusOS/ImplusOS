PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/libepoxy/src
BUILDDIR := $(PORTDIR)/build/libepoxy
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := epoxy_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libepoxy.a install

$(BUILDDIR)/libepoxy.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libepoxy.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/epoxy
	cp $(BUILDDIR)/libepoxy.a $(INSTDIR)/lib/
	cp $(SRCDIR)/epoxy/gl.h $(INSTDIR)/include/epoxy/
	cp $(SRCDIR)/epoxy/egl.h $(INSTDIR)/include/epoxy/

clean:
	rm -rf $(BUILDDIR)
