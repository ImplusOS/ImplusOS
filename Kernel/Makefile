.PHONY: all clean

ARCH ?= x86_64
KERNEL_ROOT := $(CURDIR)
ROOT_DIR := $(abspath ..)
BUILD_DIR ?= $(ROOT_DIR)/Build
KERNEL_BUILD := $(BUILD_DIR)/Kernel
KERNEL_ELF := $(KERNEL_BUILD)/Kernel_Main.ELF

include config/arch.mk

OBJS :=

include Arch/$(ARCH)/Makefile
include Boot/Makefile
include Core/Makefile
include MemoryManagement/Makefile
include Platform/Makefile
include Drivers/Makefile
include FileSystem/Makefile
include Network/Makefile
include IPC/Makefile
include Debug/Makefile

LIBC_SRCS := \
	$(ROOT_DIR)/libc/src/assert.c \
	$(ROOT_DIR)/libc/src/math.c \
	$(ROOT_DIR)/libc/src/stdlib.c \
	$(ROOT_DIR)/libc/src/string.c \
	$(ROOT_DIR)/libc/src/stdio.c \
	$(ROOT_DIR)/libc/src/errno.c

OBJS += $(patsubst $(ROOT_DIR)/libc/%.c,$(KERNEL_BUILD)/libc/%.o,$(LIBC_SRCS))

SHARELIB_SRCS := \
	$(ROOT_DIR)/ShareLib/Unicode/UTF8/UTF8.c

OBJS += $(patsubst $(ROOT_DIR)/ShareLib/%.c,$(KERNEL_BUILD)/ShareLib/%.o,$(SHARELIB_SRCS))

all: $(KERNEL_ELF)

$(KERNEL_ELF): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(KERNEL_LDFLAGS) $^ -o $@

$(KERNEL_BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(KERNEL_BUILD)/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) -f $(ARCH_ASM_FORMAT) $< -o $@

$(KERNEL_BUILD)/libc/%.o: $(ROOT_DIR)/libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(KERNEL_BUILD)/ShareLib/%.o: $(ROOT_DIR)/ShareLib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

clean:
	rm -rf $(KERNEL_BUILD)

-include $(OBJS:.o=.d)
