.PHONY: all clean

APP_NAME := com_ImplusOS_linuxhello
TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

include ../../../AppCommon.mk

NASM ?= nasm

all: $(ELF)

$(BUILD_DIR)/hello.o: hello.s
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -w-error=label-redef-late $< -o $@

$(ELF): $(BUILD_DIR)/hello.o
	$(LD) -nostdlib -e _start -Ttext 0x400000 --build-id=none -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
