.PHONY: all install clean

ARCH ?= x86_64
CROSS_COMPILE ?= x86_64-elf-
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

ROOT_DIR := ../../..
TOP_BUILD_DIR ?= $(ROOT_DIR)/Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/Service/SDL12_Compat
PREFIX ?= $(ROOT_DIR)/Userland/Application/UserApps/NetSurf/Root

LIB := $(BUILD_DIR)/libSDL.a
SRC := src/SDL12_compat.c
OBJ := $(BUILD_DIR)/SDL12_compat.o

ifeq ($(ARCH),x86_64)
ARCH_CFLAGS := -mcmodel=large -mno-red-zone
else ifeq ($(ARCH),arm64)
ARCH_CFLAGS := -mstrict-align -mno-outline-atomics
CROSS_COMPILE := aarch64-elf-
endif

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -Iinclude \
          -I$(ROOT_DIR)/libc/I_libc/include \
          -I$(ROOT_DIR) \
          -I$(ROOT_DIR)/Userland \
          -I$(ROOT_DIR)/Userland/API \
          -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP

all: $(LIB)

$(OBJ): $(SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

install: $(LIB)
	@mkdir -p $(PREFIX)/include/SDL $(PREFIX)/lib $(PREFIX)/lib/pkgconfig
	install -C -m 644 $(ROOT_DIR)/Userland/API/SDL/SDL.h $(PREFIX)/include/SDL/SDL.h
	install -C -m 644 $(LIB) $(PREFIX)/lib/libSDL.a
	sed -e 's|@PREFIX@|$(abspath $(PREFIX))|g' pkgconfig/sdl.pc.in > $(PREFIX)/lib/pkgconfig/sdl.pc

clean:
	rm -rf $(BUILD_DIR)

-include $(OBJ:.o=.d)
