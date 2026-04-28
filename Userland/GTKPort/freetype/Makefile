PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/freetype/src
BUILDDIR := $(PORTDIR)/build/freetype
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := freetype_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libfreetype.a install

$(BUILDDIR)/libfreetype.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libfreetype.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/freetype2/freetype
	cp $(BUILDDIR)/libfreetype.a $(INSTDIR)/lib/
	cp $(SRCDIR)/ft2build.h $(INSTDIR)/include/freetype2/
	cp $(SRCDIR)/freetype/freetype.h $(INSTDIR)/include/freetype2/freetype/

clean:
	rm -rf $(BUILDDIR)
