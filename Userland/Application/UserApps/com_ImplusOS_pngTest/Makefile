.PHONY: all clean copy_resources

APP_NAME := com_ImplusOS_pngTest
SRC_DIR  := .
TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := pngTest.c
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)

include ../../../AppCommon.mk

LIBPNG := $(TOP_BUILD_DIR)/Vendor/Library/libpng.a
LIBZ := $(TOP_BUILD_DIR)/Vendor/Library/libz.a
RESOURCE_DIR := Resource
OBJS := $(APP_OBJS) $(COMMON_OBJS)

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -I../../../../libc/I_libc/include -I../../../../libc/I_libc/include/sys \
          -I../../../../Userland \
          -I../../../../Userland/API \
          -I../../../../Vendor/Library/libpng \
          -I$(TOP_BUILD_DIR)/Vendor/Library/libpng/include \
          -I../../../../Vendor/Library/zlib \
          -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP
LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none -L$(TOP_BUILD_DIR)/Vendor/Library

all: $(ELF) copy_resources

copy_resources:
	@if [ -d $(RESOURCE_DIR) ]; then \
		mkdir -p $(BUILD_DIR); \
		cp -a $(RESOURCE_DIR) $(BUILD_DIR)/; \
	fi

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
    
$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(LIBPNG) $(LIBZ)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBPNG) $(LIBZ) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d) $(COMMON_DEPS)
