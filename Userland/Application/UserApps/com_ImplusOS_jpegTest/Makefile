.PHONY: all clean copy_resources

APP_NAME := com_ImplusOS_jpegTest
SRC_DIR  := .
TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := jpegTest.c
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)
JPEG_API_OBJ := $(BUILD_DIR)/API/Jpeg.o

include ../../../AppCommon.mk

LIBJPEG := $(TOP_BUILD_DIR)/Vendor/Library/libjpeg.a
RESOURCE_DIR := Resource
OBJS := $(APP_OBJS) $(JPEG_API_OBJ) $(COMMON_OBJS)

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -I../../../../libc/I_libc/include -I../../../../libc/I_libc/include/sys \
          -I../../../../Userland \
          -I../../../../Userland/API \
          -I../../../../Vendor/Library/libjpeg/src \
          -I$(TOP_BUILD_DIR)/Vendor/Library/libjpeg/include \
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

$(JPEG_API_OBJ): ../../../../Userland/API/Jpeg.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(LIBJPEG)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBJPEG) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d) $(COMMON_DEPS)
