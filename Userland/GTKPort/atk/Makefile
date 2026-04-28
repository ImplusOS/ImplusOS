PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/../..)
SRCDIR   := $(PORTDIR)/atk/src
BUILDDIR := $(PORTDIR)/build/atk
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -mcmodel=large \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(INSTDIR)/include -I$(INSTDIR)/include/glib-2.0 \
    -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include

SRCS := atk_port.c
OBJS := $(addprefix $(BUILDDIR)/, $(SRCS:.c=.o))

.PHONY: all install clean

all: $(BUILDDIR)/libatk-1.0.a install

$(BUILDDIR)/libatk-1.0.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libatk-1.0.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/atk-1.0
	cp $(BUILDDIR)/libatk-1.0.a $(INSTDIR)/lib/
	cp $(SRCDIR)/atk.h $(INSTDIR)/include/atk-1.0/

clean:
	rm -rf $(BUILDDIR)
