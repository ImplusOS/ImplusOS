CC := x86_64-elf-gcc
CXX := x86_64-elf-g++
LD := x86_64-elf-ld
AR := x86_64-elf-ar

APP_NAME := com_ImplusOS_version
SRC_DIR  := .
BUILD_DIR := ../../../../Build/Userland/SystemApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := $(shell find $(SRC_DIR) -name '*.c')
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)

include ../../../AppCommon.mk

OBJS := $(APP_OBJS) \
        $(COMMON_LIBS_DIR)/Syscalls.o \
        $(COMMON_LIBS_DIR)/libc/src/string.o \
        $(COMMON_LIBS_DIR)/libc/src/stdlib.o \
        $(COMMON_LIBS_DIR)/libc/src/errno.o \
        $(COMMON_LIBS_DIR)/libc/src/sys/syscalls.o \
        $(COMMON_LIBS_DIR)/NetworkStack/DNS/DNS.o

CFLAGS := -Os -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -ffunction-sections -fdata-sections \
          -mcmodel=large -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
          -I../../../../libc/include -I../../../../libc/include/sys \
		  -I../../../../Userland \
		  -I../../../../Userland/API \
		  -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP

CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti
LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none --gc-sections

RESOURCE_DIR := Resource

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

$(ELF): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d)
