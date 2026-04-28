PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/libxkbcommon
BUILDDIR := $(PORTDIR)/build/libxkbcommon
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR) -I$(SRCDIR)/include -I$(SRCDIR)/src \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include \
    -DHAVE_STRNDUP=1 -DDEFAULT_XKB_LAYOUT=\"us\" \
    -DDEFAULT_XKB_MODEL=\"pc105\" -DDEFAULT_XKB_OPTIONS=\"\" \
    -DDEFAULT_XKB_RULES=\"evdev\" -DDEFAULT_XKB_VARIANT=\"\" \
    -DDFLT_XKB_CONFIG_ROOT=\"/usr/share/X11/xkb\" \
    -DXLOCALEDIR=\"/usr/share/X11/locale\"

SRCS := src/atom.c src/context.c src/keysym.c src/keysym-utf.c \
    src/keymap.c src/keymap-priv.c src/state.c src/text.c src/utf8.c \
    src/utils.c src/context-priv.c src/keymap-dump.c \
    src/xkbcomp/action.c src/xkbcomp/ast-build.c src/xkbcomp/compat.c \
    src/xkbcomp/expr.c src/xkbcomp/include.c src/xkbcomp/keymap-dump.c \
    src/xkbcomp/keycodes.c src/xkbcomp/keywords.c src/xkbcomp/parser.c \
    src/xkbcomp/rules.c src/xkbcomp/scanner.c src/xkbcomp/symbols.c \
    src/xkbcomp/types.c src/xkbcomp/vmod.c src/xkbcomp/xkbcomp.c

OBJS := $(addprefix $(BUILDDIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all install clean

all: $(BUILDDIR)/libxkbcommon.a install

$(BUILDDIR)/libxkbcommon.a: $(OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/src/xkbcomp/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libxkbcommon.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/xkbcommon
	cp $(BUILDDIR)/libxkbcommon.a $(INSTDIR)/lib/
	cp $(SRCDIR)/include/xkbcommon/*.h $(INSTDIR)/include/xkbcommon/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
