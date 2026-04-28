PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/libffi
BUILDDIR := $(PORTDIR)/build/libffi
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AS       := x86_64-elf-as
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR)/include -I$(SRCDIR)/src -I$(SRCDIR)/src/x86 \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include \
    -DHAVE_LONG_DOUBLE=1 -DFFI_NO_RAW_API=0

CSRCS := src/types.c src/prep_cif.c src/closures.c src/java_raw_api.c src/raw_api.c \
    src/x86/ffi64.c

ASMSRCS := src/x86/unix64.S

COBJS := $(addprefix $(BUILDDIR)/, $(notdir $(CSRCS:.c=.o)))
ASMOBJS := $(addprefix $(BUILDDIR)/, $(notdir $(ASMSRCS:.S=.o)))

.PHONY: all install clean

all: $(BUILDDIR)/libffi.a install

$(BUILDDIR)/libffi.a: $(COBJS) $(ASMOBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/src/x86/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/src/x86/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libffi.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include
	cp $(BUILDDIR)/libffi.a $(INSTDIR)/lib/
	cp $(SRCDIR)/include/ffi.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/include/ffi_common.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/include/ffitarget.h $(INSTDIR)/include/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
