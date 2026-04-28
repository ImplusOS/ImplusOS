PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/fontconfig/src
BUILDDIR := $(PORTDIR)/build/fontconfig
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include -I$(INSTDIR)/include/freetype2 \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := fontconfig_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libfontconfig.a install

$(BUILDDIR)/libfontconfig.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libfontconfig.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/fontconfig
	cp $(BUILDDIR)/libfontconfig.a $(INSTDIR)/lib/
	cp $(SRCDIR)/fontconfig.h $(INSTDIR)/include/fontconfig/

clean:
	rm -rf $(BUILDDIR)
