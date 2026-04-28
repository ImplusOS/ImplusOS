PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/libinput
BUILDDIR := $(PORTDIR)/build/libinput
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR)/src -I$(SRCDIR)/include \
    -I$(INSTDIR)/include -I$(INSTDIR)/include/libevdev-1.0 \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include \
    -DHAVE_LIBEVDEV=1

SRCS := src/libinput.c src/libinput-util.c src/evdev.c src/evdev-mt-touchpad.c \
    src/evdev-mt-touchpad-tap.c src/evdev-mt-touchpad-buttons.c \
    src/evdev-mt-touchpad-edge-scroll.c src/evdev-mt-touchpad-gestures.c \
    src/evdev-mt-touchpad-thumb.c src/evdev-fallback.c \
    src/evdev-middle-button.c src/evdev-debounce.c \
    src/evdev-totem.c src/evdev-tablet.c src/evdev-tablet-pad.c \
    src/evdev-tablet-pad-leds.c src/filter.c src/filter-flat.c \
    src/filter-low-dpi.c src/filter-mouse.c src/filter-touchpad.c \
    src/filter-touchpad-flat.c src/filter-trackpoint.c src/filter-trackpoint-flat.c \
    src/path-seat.c src/timer.c src/quirks.c

OBJS := $(addprefix $(BUILDDIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all install clean

all: $(BUILDDIR)/libinput.a install

$(BUILDDIR)/libinput.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libinput.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include
	cp $(BUILDDIR)/libinput.a $(INSTDIR)/lib/
	cp $(SRCDIR)/src/libinput.h $(INSTDIR)/include/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
