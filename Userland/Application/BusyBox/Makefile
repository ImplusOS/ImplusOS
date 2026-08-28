.PHONY: all clean copy_resources fetch_busybox

APP_NAME := $(notdir $(patsubst %/,%,$(CURDIR)))
SRC_DIR  := .
TOP_BUILD_DIR ?= ../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := $(shell find $(SRC_DIR) -name '*.c')
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)

include ../../AppCommon.mk

OBJS := $(APP_OBJS) $(COMMON_OBJS)

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -I../../../libc/I_libc/include -I../../../libc/I_libc/include/sys \
          -I../../../Userland \
          -I../../../Userland/API \
          -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP
LDFLAGS := -T ../../../Userland/Userland.ld -e _start -nostdlib --build-id=none

RESOURCE_DIR := Resource

BUSYBOX_VERSION ?= 1.35.0
BUSYBOX_URL     ?= https://busybox.net/downloads/binaries/$(BUSYBOX_VERSION)-x86_64-linux-musl/busybox
BUSYBOX_BIN     := $(RESOURCE_DIR)/busybox
BUSYBOX_SHA_FILE := $(RESOURCE_DIR)/busybox.sha256
CURL            ?= curl

ifeq ($(ARCH),x86_64)
all: fetch_busybox $(ELF) copy_resources
copy_resources: fetch_busybox
else
$(warning BusyBox app: no prebuilt static BusyBox for ARCH=$(ARCH); staging launcher only)
all: $(ELF) copy_resources
endif

fetch_busybox: $(BUSYBOX_BIN)

$(BUSYBOX_BIN):
	@mkdir -p $(RESOURCE_DIR)
	@echo "  FETCH   $(BUSYBOX_URL)"
	@$(CURL) -fL --retry 3 --connect-timeout 15 -o $@.tmp "$(BUSYBOX_URL)"
	@if [ -s "$(BUSYBOX_SHA_FILE)" ]; then \
		printf '%s  %s\n' "$$(tr -d ' \t\r\n' < $(BUSYBOX_SHA_FILE))" "$@.tmp" | sha256sum -c - ; \
	fi
	@od -An -tx1 -N4 $@.tmp | tr -s ' ' | grep -qi '7f 45 4c 46' || { \
		echo "error: fetched BusyBox is not an ELF binary ($(BUSYBOX_URL))" >&2; \
		rm -f $@.tmp; exit 1; }
	@chmod +x $@.tmp
	@mv $@.tmp $@

copy_resources:
	@if [ -d $(RESOURCE_DIR) ]; then \
		mkdir -p $(BUILD_DIR); \
		cp -a $(RESOURCE_DIR) $(BUILD_DIR)/; \
	fi

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(BUSYBOX_BIN).tmp

distclean: clean
	rm -f $(BUSYBOX_BIN)

-include $(APP_OBJS:.o=.d) $(COMMON_DEPS)
