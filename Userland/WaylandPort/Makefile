PORTDIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
TOPDIR  := $(abspath $(PORTDIR)/..)
SRCDIR  := $(PORTDIR)/src
BUILDDIR:= $(PORTDIR)/build
INSTDIR := $(PORTDIR)/sysroot
CC      := x86_64-elf-gcc
AR      := x86_64-elf-ar
RANLIB  := x86_64-elf-ranlib

CFLAGS_COMMON := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
    -I$(TOPDIR)/musl/arch/implus -I$(TOPDIR)/musl/include \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/musl/src/internal \
    -I$(TOPDIR)/libc/include -I$(INSTDIR)/include \
    -I$(PORTDIR)/adapters/include \
    -DHAVE_PTHREADS=0 -D_GNU_SOURCE

.PHONY: all clean pixman libdrm libxkbcommon libevdev libinput libffi libwayland wayland_protocols mesa

all: pixman libdrm libxkbcommon libevdev libinput libffi libwayland wayland_protocols mesa
	@echo "=== All Wayland stack libraries built ==="

pixman:
	$(MAKE) -f $(PORTDIR)/pixman/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

libdrm:
	$(MAKE) -f $(PORTDIR)/libdrm/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

libxkbcommon:
	$(MAKE) -f $(PORTDIR)/libxkbcommon/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

libevdev:
	$(MAKE) -f $(PORTDIR)/libevdev/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

libinput:
	$(MAKE) -f $(PORTDIR)/libinput/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

libffi:
	$(MAKE) -f $(PORTDIR)/libffi/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

libwayland:
	$(MAKE) -f $(PORTDIR)/libwayland/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

wayland_protocols:
	$(MAKE) -f $(PORTDIR)/wayland_protocols/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

mesa:
	$(MAKE) -f $(PORTDIR)/mesa/Makefile PORTDIR=$(PORTDIR) TOPDIR=$(TOPDIR)

clean:
	rm -rf $(BUILDDIR) $(INSTDIR)
