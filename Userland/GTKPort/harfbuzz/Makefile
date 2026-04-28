PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/harfbuzz/src
BUILDDIR := $(PORTDIR)/build/harfbuzz
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include -I$(INSTDIR)/include/freetype2 \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := harfbuzz_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libharfbuzz.a install

$(BUILDDIR)/libharfbuzz.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libharfbuzz.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/harfbuzz
	cp $(BUILDDIR)/libharfbuzz.a $(INSTDIR)/lib/
	cp $(SRCDIR)/hb.h $(INSTDIR)/include/harfbuzz/
	cp $(SRCDIR)/hb-ft.h $(INSTDIR)/include/harfbuzz/

clean:
	rm -rf $(BUILDDIR)
