.PHONY: all clean

APP_NAME := com_ImplusOS_zlibTest
SRC_DIR  := .
TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := zlibTest.c Zlib_Impl.c
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)

include ../../../AppCommon.mk

LIBZ := $(TOP_BUILD_DIR)/Vendor/Library/libz.a
OBJS := $(APP_OBJS) $(COMMON_OBJS)

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin -DNO_GZFILE_OPS \
          $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -I../../../../libc/I_libc/include -I../../../../libc/I_libc/include/sys \
		  -I../../../../Userland \
		  -I../../../../Userland/API \
		  -I../../../../Vendor/Library/zlib \
		  -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP
LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none -L$(TOP_BUILD_DIR)/Vendor/Library

all: $(ELF)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(LIBZ)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) -lz -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d) $(COMMON_DEPS)
