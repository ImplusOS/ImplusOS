# Userland/Application/UserApps/com_ImplusOS_ffmpegTest/Makefile
.PHONY: all clean copy_resources

APP_NAME := com_ImplusOS_ffmpegTest
SRC_DIR  := .
TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := ffmpegTest.c
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)
FFMPEG_API_OBJ := $(BUILD_DIR)/API/FFmpeg.o

include ../../../AppCommon.mk

LIBAVFORMAT := $(TOP_BUILD_DIR)/Vendor/Library/libavformat.a
LIBAVCODEC  := $(TOP_BUILD_DIR)/Vendor/Library/libavcodec.a
LIBAVUTIL   := $(TOP_BUILD_DIR)/Vendor/Library/libavutil.a
LIBSWSCALE  := $(TOP_BUILD_DIR)/Vendor/Library/libswscale.a
RESOURCE_DIR := Resource
OBJS := $(APP_OBJS) $(FFMPEG_API_OBJ) $(COMMON_OBJS)

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -mcmodel=large $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -Os -ffunction-sections -fdata-sections \
          -DIMPLUSOS_FFMPEG_BUILD \
          -I../../../../libc/I_libc/include -I../../../../libc/I_libc/include/sys \
          -I../../../../Userland \
          -I../../../../Userland/API \
           -I../../../../Vendor/Library/ffmpeg \
           -I$(TOP_BUILD_DIR)/Vendor/Library/ffmpeg \
          -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP
LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none -L$(TOP_BUILD_DIR)/Vendor/Library --gc-sections --allow-multiple-definition

all: $(ELF) copy_resources

copy_resources:
	@if [ -d $(RESOURCE_DIR) ]; then \
		mkdir -p $(BUILD_DIR); \
		cp -a $(RESOURCE_DIR) $(BUILD_DIR)/; \
	fi

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(FFMPEG_API_OBJ): ../../../../Userland/API/FFmpeg.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(LIBAVFORMAT) $(LIBAVCODEC) $(LIBAVUTIL) $(LIBSWSCALE)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBAVFORMAT) $(LIBAVCODEC) $(LIBSWSCALE) $(LIBAVUTIL) $(USERLAND_LIBGCC) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d) $(COMMON_DEPS)