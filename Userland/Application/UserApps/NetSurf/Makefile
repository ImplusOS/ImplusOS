ARCH ?= x86_64
REPO_ROOT := $(abspath ../../../..)
TOP_BUILD_DIR ?= $(REPO_ROOT)/Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/NetSurf
APP_NAME := NetSurf

# Toolchain
ifeq ($(ARCH),x86_64)
CROSS_COMPILE ?= x86_64-elf-
ARCH_CFLAGS := -mcmodel=large -mno-red-zone
endif
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

# Staging prefix (prebuilt libraries/headers)
PREFIX := $(REPO_ROOT)/Userland/Application/UserApps/NetSurf/Root

# NetSurf source
NETSURF_CHECKOUT := $(REPO_ROOT)/Userland/Application/UserApps/NetSurf/Source/netsurf
NETSURF_BUILDSYSTEM := $(REPO_ROOT)/Userland/Application/UserApps/NetSurf/netsurf-buildsystem

# Vendor libraries
VENDOR_DIR := $(REPO_ROOT)/Vendor/Library

# Service layer
SERVICE_DIR := $(REPO_ROOT)/Userland/Service/NetSurf
SHIM_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/NetSurfObjects

# API/Libc paths
API_INCLUDES := $(REPO_ROOT)/Userland/API
LIBC_INCLUDES := $(REPO_ROOT)/libc/I_libc/include

# Prebuilt libc/syscall objects (from top-level build)
COMMON_OBJ_DIR := $(TOP_BUILD_DIR)/Userland
LIBC_OBJ_DIR := $(COMMON_OBJ_DIR)/libc/I_libc/src
SYS_OBJ_DIR := $(LIBC_OBJ_DIR)/sys/$(ARCH)

# Runtime resources
RESOURCE_SRC := $(NETSURF_CHECKOUT)/frontends/framebuffer/res
CHOICES_SRC := $(REPO_ROOT)/Userland/Application/UserApps/NetSurf/Choices
CA_BUNDLE ?= $(firstword $(wildcard \
	/etc/ssl/certs/ca-certificates.crt \
	/etc/ssl/cert.pem \
	/opt/homebrew/etc/openssl@3/cert.pem \
	/opt/homebrew/etc/openssl@1.1/cert.pem \
	/usr/local/etc/openssl@3/cert.pem))

# Output
ELF := $(BUILD_DIR)/$(APP_NAME).ELF

# NetSurf's frameduffer build output
NS_BUILD_DIR := $(NETSURF_CHECKOUT)/build/$(shell uname -s)-framebuffer

# Makefile.config for NetSurf
NS_CONFIG := $(NETSURF_CHECKOUT)/Makefile.config

.PHONY: all clean verify_checkout vendor_libs copy_resources

all: vendor_libs $(ELF) copy_resources

verify_checkout:
	@echo "Verifying NetSurf checkout..."
	@cd $(NETSURF_CHECKOUT) && git diff --quiet --exit-code HEAD || \
		(echo "ERROR: NetSurf checkout has uncommitted changes!"; exit 1)
	@echo "NetSurf checkout clean."

vendor_libs:
	+$(MAKE) -C $(SERVICE_DIR) all ARCH=$(ARCH)

# Build NetSurf objects using NetSurf's own build system
$(NS_BUILD_DIR)/created: vendor_libs
	rm -f $(NS_CONFIG)
	rm -rf $(NS_BUILD_DIR)
	mkdir -p $(NS_BUILD_DIR)/deps
	cp $(SERVICE_DIR)/Makefile.config.implusos $(NS_CONFIG)
	+CFLAGS="-ffreestanding -fno-pic $(ARCH_CFLAGS) -nostdlib -O2 \
			-I$(LIBC_INCLUDES) -I$(LIBC_INCLUDES)/sys \
			-I$(REPO_ROOT)/Userland -I$(API_INCLUDES) \
			-I$(PREFIX)/include \
			-Wno-unused-parameter -Wno-unused-function" \
		LDFLAGS="-L$(PREFIX)/lib -nostdlib -Wl,--unresolved-symbols=ignore-in-object-files" \
		BUILD_CFLAGS="-g -I/opt/homebrew/include" \
		BUILD_LDFLAGS="-L/opt/homebrew/lib" \
		BUILD_LIBPNG_CFLAGS="-I/opt/homebrew/include" \
		BUILD_LIBPNG_LDFLAGS="-L/opt/homebrew/lib -lpng" \
		PKG_CONFIG_PATH="$(PREFIX)/lib/pkgconfig" \
		MAKEFLAGS= $(MAKE) -j1 -C $(NETSURF_CHECKOUT) \
		TARGET=framebuffer \
		CC="$(CC)" \
		AR="$(AR)" \
		BUILD_CC=cc \
		NETSURF_FB_RESPATH="/Userland/UserApps/NetSurf/res" \
		NETSURF_FB_FONTLIB=internal \
		NETSURF_FB_FRONTEND=sdl \
		NETSURF_HOMEPAGE="https://example.com/" \
		all-program 2>&1
	touch $@

# Archive NetSurf objects into a static library (bypass ARG_MAX)
$(BUILD_DIR)/libnetsurf_objs.a: $(NS_BUILD_DIR)/created
	@mkdir -p $(dir $@)
	cd $(NS_BUILD_DIR) && $(AR) rcs $@ *.o 2>/dev/null || true
	ls $@ >/dev/null 2>&1

# netsurf_start.o
$(BUILD_DIR)/netsurf_start.o: netsurf_start.c
	@mkdir -p $(dir $@)
	$(CC) -ffreestanding -fno-stack-protector -fno-pic $(ARCH_CFLAGS) \
		-nostdlib -nostartfiles -nodefaultlibs -O2 \
		-I$(LIBC_INCLUDES) -I$(LIBC_INCLUDES)/sys \
		-I$(REPO_ROOT)/Userland -I$(API_INCLUDES) \
		-I$(PREFIX)/include \
		-Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-MMD -MP -c $< -o $@

# Service shim objects
$(SHIM_DIR)/mbedtls_implusos.o: $(SERVICE_DIR)/MbedTlsShim/mbedtls_implusos.c
	@mkdir -p $(dir $@)
	$(CC) -ffreestanding -fno-stack-protector -fno-pic $(ARCH_CFLAGS) \
		-nostdlib -nostartfiles -nodefaultlibs -O2 \
		-I$(LIBC_INCLUDES) -I$(LIBC_INCLUDES)/sys \
		-I$(REPO_ROOT)/Userland -I$(API_INCLUDES) \
		-I$(PREFIX)/include \
		-Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-MMD -MP -c $< -o $@

$(SHIM_DIR)/gzshim.o: $(SERVICE_DIR)/ZlibGzShim/gzshim.c
	@mkdir -p $(dir $@)
	$(CC) -ffreestanding -fno-stack-protector -fno-pic $(ARCH_CFLAGS) \
		-nostdlib -nostartfiles -nodefaultlibs -O2 \
		-I$(LIBC_INCLUDES) -I$(LIBC_INCLUDES)/sys \
			-I$(REPO_ROOT)/Userland -I$(API_INCLUDES) \
			-Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
			-MMD -MP -c $< -o $@

$(SHIM_DIR)/netsurf_init_trace.o: $(SERVICE_DIR)/InitTraceShim/netsurf_init_trace.c
	@mkdir -p $(dir $@)
	$(CC) -ffreestanding -fno-stack-protector -fno-pic $(ARCH_CFLAGS) \
		-nostdlib -nostartfiles -nodefaultlibs -O2 \
		-I$(LIBC_INCLUDES) -I$(LIBC_INCLUDES)/sys \
		-I$(REPO_ROOT)/Userland -I$(API_INCLUDES) \
		-I$(PREFIX)/include \
		-Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
		-MMD -MP -c $< -o $@

# Link
$(ELF): $(BUILD_DIR)/libnetsurf_objs.a $(BUILD_DIR)/netsurf_start.o $(SHIM_DIR)/mbedtls_implusos.o $(SHIM_DIR)/gzshim.o $(SHIM_DIR)/netsurf_init_trace.o
	@mkdir -p $(dir $@)
	$(LD) -T $(REPO_ROOT)/Userland/Userland.ld -e _start -nostdlib --build-id=none \
		--wrap=corestrings_init \
		--wrap=nscolour_update \
		--wrap=image_cache_init \
		--wrap=nscss_init \
		--wrap=html_init \
		--wrap=image_init \
		--wrap=textplain_init \
		--wrap=fetcher_init \
		--wrap=hlcache_initialise \
		--wrap=llcache_initialise \
		--wrap=ns_system_colour_init \
		--wrap=page_info_init \
		--wrap=fetch_curl_register \
		--wrap=fetch_data_register \
		--wrap=fetch_file_register \
		--wrap=fetch_resource_register \
		--wrap=fetch_about_register \
		--wrap=fetch_javascript_register \
		--wrap=content_factory_register_handler \
		--wrap=curl_global_init \
		--wrap=curl_multi_init \
		--wrap=curl_multi_setopt \
		--wrap=curl_easy_init \
		--wrap=curl_easy_setopt \
		$(BUILD_DIR)/netsurf_start.o \
		$(SHIM_DIR)/netsurf_init_trace.o \
		$(BUILD_DIR)/libnetsurf_objs.a \
		$(SHIM_DIR)/mbedtls_implusos.o \
		$(SHIM_DIR)/gzshim.o \
		-L$(PREFIX)/lib \
		-L$(COMMON_OBJ_DIR) \
		-L$(LIBC_OBJ_DIR) \
		-L$(SYS_OBJ_DIR) \
		$(COMMON_OBJ_DIR)/Syscalls.o \
		$(LIBC_OBJ_DIR)/sys/syscalls.o \
		$(LIBC_OBJ_DIR)/string.o \
		$(LIBC_OBJ_DIR)/iconv.o \
		$(LIBC_OBJ_DIR)/stdlib.o \
		$(LIBC_OBJ_DIR)/errno.o \
		$(LIBC_OBJ_DIR)/posix.o \
		$(LIBC_OBJ_DIR)/stdio.o \
		$(LIBC_OBJ_DIR)/math.o \
		$(LIBC_OBJ_DIR)/assert.o \
		$(COMMON_OBJ_DIR)/API/XMLParser.o \
		$(COMMON_OBJ_DIR)/NetworkStack/DNS/DNS.o \
		$(SYS_OBJ_DIR)/hal_syscall.o \
		$(SYS_OBJ_DIR)/setjmp.o \
		--start-group \
		-lnsfb -lSDL -lcurl -lcss -ldom -lhubbub -lnsutils \
		-lparserutils -lwapcaplet -lnsgif -lnsbmp -lpng -lz \
		-lmbedtls -lmbedx509 -lmbedcrypto \
		--end-group \
		"$(LIBGCC)" \
		-o $@

copy_resources:
	@mkdir -p $(BUILD_DIR)
	rm -rf $(BUILD_DIR)/res
	cp -RL $(RESOURCE_SRC) $(BUILD_DIR)/res
	cp $(CHOICES_SRC) $(BUILD_DIR)/Choices
	cp $(CHOICES_SRC) $(BUILD_DIR)/res/Choices
	@if [ -z "$(CA_BUNDLE)" ]; then \
		echo "ERROR: no CA certificate bundle found for NetSurf staging"; \
		exit 1; \
	fi
	cp $(CA_BUNDLE) $(BUILD_DIR)/res/ca-certificates.crt
	@find $(BUILD_DIR)/res -type f -name Messages -print | while read -r f; do \
		if gzip -t "$$f" >/dev/null 2>&1; then \
			gzip -dc "$$f" > "$$f.tmp" && mv "$$f.tmp" "$$f"; \
		fi; \
	done

clean:
	+$(MAKE) -C $(NETSURF_CHECKOUT) TARGET=framebuffer clean 2>/dev/null || true
	rm -f $(NS_CONFIG)
	rm -rf $(BUILD_DIR)
	rm -rf $(NETSURF_CHECKOUT)/build

-include $(BUILD_DIR)/netsurf_start.d
