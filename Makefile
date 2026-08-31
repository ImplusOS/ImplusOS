.ONESHELL:
SHELL := /bin/bash
.SHELLFLAGS := -euxo pipefail -c

export MTOOLSRC := /dev/null

.PHONY: all kernel app_build service_build driver_build driver_stage recovery_build install_payload \
        image image_livecd qemu_input_status run_uefi_usb run_uefi_cdrom run_bios_cdrom \
        qemu_disks clean edk2_bootloader edk2_bootmanager vendor_libs linux_runtime_stage

# ── Host OS detection ──────────────────────────────────────────────
HOST_OS := $(shell uname -s 2>/dev/null || echo Unknown)

ifeq ($(HOST_OS),Darwin)
  # macOS: Apple Silicon → /opt/homebrew, Intel → /usr/local
  UNAME_M := $(shell uname -m 2>/dev/null)
  ifeq ($(UNAME_M),arm64)
    HOMEBREW_PREFIX ?= /opt/homebrew
  else
    HOMEBREW_PREFIX ?= /usr/local
  endif
else ifeq ($(HOST_OS),Linux)
  HOMEBREW_PREFIX ?= /home/linuxbrew/.linuxbrew
else
  $(warning Unsupported host OS '$(HOST_OS)'. Defaults may not work.)
  HOMEBREW_PREFIX ?= /home/linuxbrew/.linuxbrew
endif

export HOMEBREW_PREFIX

ARCH ?= x86_64

BUILD_ROOT ?= Build
BUILD_DIR ?= $(BUILD_ROOT)/$(ARCH)
IMAGE_DIR := Image
IMAGE := $(IMAGE_DIR)/ImplusOS-$(ARCH)-InstallMedia.iso

EDK2_DIR ?= $(HOME)/edk2
EDK2_TARGET ?= RELEASE
EDK2_TOOLCHAIN ?= CLANGDWARF

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-elf-
EDK2_ARCH := AARCH64
BOOTLOADER_EFI := $(BUILD_DIR)/Loader/BOOTAA64.EFI
QEMU_FIRMWARE := $(CURDIR)/AAVMF_CODE.fd
USERLAND_ARCH_CFLAGS := -mstrict-align -mno-outline-atomics
USERLAND_ARCH_CXXFLAGS := -mstrict-align -mno-outline-atomics
else ifeq ($(ARCH),x86_64)
CROSS_COMPILE ?= x86_64-elf-
EDK2_ARCH := X64
BOOTLOADER_EFI := $(BUILD_DIR)/Loader/BOOTX64.EFI
QEMU_FIRMWARE := $(CURDIR)/OVMF_CODE_4M.fd
USERLAND_ARCH_CFLAGS := -mcmodel=large -mno-red-zone
USERLAND_ARCH_CXXFLAGS := -mcmodel=large -mno-red-zone
else
$(error Unsupported ARCH '$(ARCH)'. Use x86_64 or arm64.)
endif

CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
NASM := nasm

KERNEL_DIR   := Kernel
RECOVERY_DIR := RecoveryEnvironment/Source

KERNEL_ELF        := $(BUILD_DIR)/Kernel/Kernel_Main.ELF
BOOTMANAGER_EFI   := $(BUILD_DIR)/BootManager/BOOTMANAGER.EFI
USERLAND_INIT_ELF := $(BUILD_DIR)/Userland/Userland.ELF
RECOVERY_INIT_ELF := $(BUILD_DIR)/RecoveryEnvironment/Userland.ELF

INSTALL_PAYLOAD_DIR  := $(BUILD_DIR)/InstallPayload
INSTALL_PAYLOAD_ROOT := $(INSTALL_PAYLOAD_DIR)/root
INSTALL_PAYLOAD_TGZ  := $(INSTALL_PAYLOAD_DIR)/ImplusOS-root.tar.gz
INSTALL_DISK_IMAGE   := $(INSTALL_PAYLOAD_DIR)/ImplusOS-install.img
INSTALL_MANIFEST     := $(INSTALL_PAYLOAD_DIR)/MANIFEST.txt
# Bumped from 256: the install-media FAT image now also carries the Chromium
# resource tree (~660 MiB) + the external Linux runtime .so closure (~250 MiB)
# + C.UTF-8 (Docs/Others/TODO_glibc_Port.md §7-3). Shrink once measured.
INSTALL_DISK_IMAGE_SIZE_MB ?= 2560

IMAGE_STAGE_DIR := $(BUILD_DIR)/ISO_ROOT
ESP_IMAGE       := $(IMAGE_DIR)/esp-$(ARCH).img
RECOVERY_ESP_IMAGE_SIZE_MB ?= 16

# External Linux runtime (glibc dynamic linker + .so closure + C.UTF-8) staged
# for external Linux-ABI binaries such as Chromium. x86_64 only; always bundled
# (Docs/Others/TODO_glibc_Port.md §7-3 -- no opt-out). Build rules live in
# Vendor/LinuxRuntime/Makefile; this file only delegates.
LINUX_RUNTIME_DIR   := Vendor/LinuxRuntime
LINUX_RUNTIME_STAGE := $(BUILD_DIR)/LinuxRuntime/stage
LIVECD_ESP_IMAGE_SIZE_MB ?= 16

LIVECD_IMAGE := $(IMAGE_DIR)/ImplusOS-$(ARCH)-LiveCD.iso

BOOT_RESOURCE_DIR := $(firstword $(wildcard BootManager/Source/Resource))
ifeq ($(BOOT_RESOURCE_DIR),)
$(error BootManager resource directory not found. Expected BootManager/Source/Resource.)
endif

BOOTLOADER_DSC := BootLoader/Source/Configuration/ImplusOSBootLoader.dsc
BOOTMANAGER_DSC := BootManager/Source/BootManager.dsc

EDK2_OUTPUT_ROOT := $(EDK2_DIR)/Build/$(EDK2_TARGET)_$(EDK2_TOOLCHAIN)/$(EDK2_ARCH)
BOOTLOADER_MODULE_NAME := ImplusOSBootLoader
BOOTMANAGER_MODULE_NAME := BootManager

define EDK2_BUILD_MODULE
	cd $(EDK2_DIR) && \
	export PYTHON_COMMAND=python3 && \
	set +u && \
	. ./edksetup.sh && \
	set -u && \
	export WORKSPACE=$(EDK2_DIR) && \
	export PACKAGES_PATH=$(CURDIR) && \
	build \
		-a $(EDK2_ARCH) \
		-t $(EDK2_TOOLCHAIN) \
		-b $(EDK2_TARGET) \
		-p $(abspath $(1))
endef

define COPY_EDK2_OUTPUT
	src="$$(find "$(EDK2_OUTPUT_ROOT)" -type f \( -name '$(1).efi' -o -name '$(1).EFI' \) | sort | head -n 1)"; \
	if [ -z "$$src" ]; then \
		echo "Missing EDK2 output for $(1): $(EDK2_OUTPUT_ROOT)"; \
		find "$(EDK2_OUTPUT_ROOT)" -type f \( -name '*.efi' -o -name '*.EFI' \) | sort || true; \
		exit 1; \
	fi; \
	mkdir -p "$(dir $(2))"; \
	cp "$$src" "$(2)"
endef

# mindepth 2 excludes Kernel/Source/Drivers/Makefile itself (a source-list
# snippet $(include)'d by Kernel/Source/Makefile, not a buildable module
# directory) while still matching every driver module's own Makefile under
# Kernel/Source/Drivers/<Category>/<Driver>/ (see
# Docs/Others/TODO_OS_Refactor.md 4./5./6.).
DRIVER_MAKEFILES := $(shell find Kernel/Source/Drivers -mindepth 2 -name Makefile -print 2>/dev/null | sort)
DRIVER_DIRS      := $(sort $(patsubst %/,%,$(dir $(DRIVER_MAKEFILES))))
DRIVER_BUILD_ROOT := $(BUILD_ROOT)/Modules
DRIVER_STAGE_DIR  := $(BUILD_DIR)/Kernel/Drivers

# Firmware blobs staged verbatim (not built) into Kernel/Driver/Firmware/ on
# the boot image (the boot image path convention is kept as-is; only the
# source-tree location moved -- see Docs/Others/TODO_OS_Refactor.md 4.). Every
# immediate subdirectory of FIRMWARE_SRC_DIR (e.g.
# Kernel/Drivers/Firmware/AX900/, see its README.md) is copied over as-is by
# the STAGE_FIRMWARE*/loops below -- adding a new device's firmware
# directory needs no Makefile change. Ships empty by default; this only
# wires up the staging, it does not provide any proprietary bytes.
FIRMWARE_SRC_DIR := Kernel/Source/Drivers/Firmware

# Driver .ELFs listed here are staged into Kernel/Driver/OnDemand/ instead
# of Kernel/Driver/ itself: the bootloader preloads every *.ELF directly
# under Kernel/Driver/ at every boot (see BootManager/UEFI's
# PreloadDriverModules, which does not recurse into subdirectories), so
# anything meant to be loaded only when actually needed --
# driver_module_manager_load_from_vfs(), driven by BusRegistry.c consulting
# DRIVER_DB_SRC below -- has to live one level down where the preloader
# never sees it. The list is data, not a hard-coded driver name: one .ELF
# basename per line in the manifest below (blank lines and '#' comments
# ignored). Absent manifest => every driver .ELF is preloaded.
ON_DEMAND_MANIFEST := Kernel/Source/Drivers/Manifest/OnDemand.txt
ON_DEMAND_DRIVER_ELFS := $(if $(wildcard $(ON_DEMAND_MANIFEST)),$(shell sed -e 's/#.*//' -e '/^[[:space:]]*$$/d' $(ON_DEMAND_MANIFEST)))
DRIVER_DB_SRC := Kernel/Source/Drivers/Manifest/DriverDB.txt

# Statically-linked BusyBox vendored by the BusyBox app (fetched by its own
# Makefile). Also published as /bin/sh -- see STAGE_POSIX_BIN below.
BUSYBOX_BIN := Userland/Application/BusyBox/Resource/busybox

# Copies $(DRIVER_STAGE_DIR)/*.ELF into $(1)/Kernel/Driver/ or
# $(1)/Kernel/Driver/OnDemand/ depending on ON_DEMAND_DRIVER_ELFS.
define STAGE_DRIVER_ELFS
	for f in $(DRIVER_STAGE_DIR)/*.ELF; do \
		[ -e "$$f" ] || continue; \
		base=$$(basename "$$f"); \
		case " $(ON_DEMAND_DRIVER_ELFS) " in \
			*" $$base "*) cp "$$f" "$(1)/Kernel/Driver/OnDemand/";; \
			*) cp "$$f" "$(1)/Kernel/Driver/";; \
		esac; \
	done
endef

# Copies every immediate subdirectory of $(FIRMWARE_SRC_DIR) (e.g. AX900/)
# verbatim into $(1)/Kernel/Driver/Firmware/<name>/, creating the
# destination directory as needed. Generic over whatever subdirectories
# exist -- no per-device name baked in here.
define STAGE_FIRMWARE
	for d in $(FIRMWARE_SRC_DIR)/*/; do \
		[ -d "$$d" ] || continue; \
		name=$$(basename "$$d"); \
		mkdir -p "$(1)/Kernel/Driver/Firmware/$$name"; \
		cp -a "$$d." "$(1)/Kernel/Driver/Firmware/$$name/"; \
	done
endef

# Populates $(1)/bin with the statically-linked BusyBox already vendored for
# the BusyBox app, published under the POSIX names foreign programs expect.
#
# /bin/sh in particular is not optional: Xorg runs xkbcomp -- the only way it
# can compile a keymap, and a hard requirement for bringing up the virtual
# core keyboard -- through Popen(), which is fork() + execl("/bin/sh", "sh",
# "-c", ...). With no /bin/sh the exec fails and X dies with "XKB: Could not
# invoke xkbcomp". BusyBox is static (musl), so it needs no ld.so or .so
# closure, and its ash dispatches on argv[0] == "sh".
#
# Copied rather than symlinked: the ISO9660 driver's symlink support is not
# exercised on this path, and BusyBox is ~1 MB.
define STAGE_POSIX_BIN
	if [ -f "$(BUSYBOX_BIN)" ]; then \
		mkdir -p "$(1)/bin"; \
		cp "$(BUSYBOX_BIN)" "$(1)/bin/busybox"; \
		cp "$(BUSYBOX_BIN)" "$(1)/bin/sh"; \
		chmod 0755 "$(1)/bin/busybox" "$(1)/bin/sh"; \
	else \
		echo "warning: $(BUSYBOX_BIN) missing; /bin/sh will be absent (Xorg cannot run xkbcomp)" >&2; \
	fi
endef

# Same, but into a mounted mtools FAT image ($(1) = image path) via
# mmd/mcopy instead of mkdir/cp.
define STAGE_FIRMWARE_FAT
	for d in $(FIRMWARE_SRC_DIR)/*/; do \
		[ -d "$$d" ] || continue; \
		name=$$(basename "$$d"); \
		mmd -i $(1) "::/Kernel/Driver/Firmware/$$name"; \
		for f in "$$d"*; do \
			[ -e "$$f" ] || continue; \
			mcopy -o -i $(1) "$$f" "::/Kernel/Driver/Firmware/$$name/"; \
		done; \
	done;
endef

# Each app/service is now its own top-level repository (github.com/ImplusOS/
# <name>) instead of living under a shared Userland/Application/ or
# Userland/Service/ parent, so each is named explicitly here and guarded
# with $(wildcard ...) for one not yet checked out as a submodule.
APP_REPO_NAMES := com.ImplusOS.sysnotif com.ImplusOS.waylandcompositor \
                   com.ImplusOS.gtk3demo com.ImplusOS.windowmanager
SERVICE_REPO_NAMES := com.ImplusOS.ldso com.ImplusOS.dynmain \
                       com.ImplusOS.posix com.ImplusOS.netstack

APP_DIRS := $(foreach n,$(APP_REPO_NAMES),$(if $(wildcard $(n)/Makefile),$(n)))
SERVICE_DIRS := $(foreach n,$(SERVICE_REPO_NAMES),$(if $(wildcard $(n)/Makefile),$(n)))

LIBRARY_C_SRCS := $(shell find Library/Source -name "*.c" 2>/dev/null)

USERLAND_C_SRCS := \
	libc/I_libc/Source/src/assert.c \
	libc/I_libc/Source/src/math.c \
	libc/I_libc/Source/src/stdlib.c \
	libc/I_libc/Source/src/string.c \
	libc/I_libc/Source/src/iconv.c \
	libc/I_libc/Source/src/stdio.c \
	libc/I_libc/Source/src/errno.c \
	libc/I_libc/Source/src/posix.c \
	libc/I_libc/Source/src/dlfcn.c \
	libc/I_libc/Source/src/sys/syscalls.c \
	libc/I_libc/Source/src/sys/$(ARCH)/hal_syscall.c \
	libc/I_libc/Source/src/sys/$(ARCH)/setjmp.c \
	$(LIBRARY_C_SRCS) \
	Userland/Source/Userland.c \
	Userland/Source/Syscalls.c \
	Userland/API/Source/XMLParser.c \
	Userland/Service/Source/service_client.c \
	com.ImplusOS.netstack/Source/DNS/DNS.c \
	com.ImplusOS.posix/Source/src/posix_fdtable.c \
	com.ImplusOS.posix/Source/src/posix_file.c \
	com.ImplusOS.posix/Source/src/posix_process.c \
	com.ImplusOS.posix/Source/src/posix_signal.c \
	com.ImplusOS.posix/Source/src/posix_thread.c \
	com.ImplusOS.posix/Source/src/posix_net.c \
	com.ImplusOS.posix/Source/src/posix_time.c \
	com.ImplusOS.posix/Source/src/posix_mman.c \
	com.ImplusOS.posix/Source/src/posix_io.c

USERLAND_APP_C_SRCS := \
	libc/I_libc/Source/src/assert.c \
	libc/I_libc/Source/src/math.c \
	libc/I_libc/Source/src/stdlib.c \
	libc/I_libc/Source/src/string.c \
	libc/I_libc/Source/src/iconv.c \
	libc/I_libc/Source/src/stdio.c \
	libc/I_libc/Source/src/errno.c \
	libc/I_libc/Source/src/posix.c \
	libc/I_libc/Source/src/dlfcn.c \
	libc/I_libc/Source/src/sys/syscalls.c \
	libc/I_libc/Source/src/sys/$(ARCH)/hal_syscall.c \
	libc/I_libc/Source/src/sys/$(ARCH)/setjmp.c \
	$(LIBRARY_C_SRCS) \
	Userland/Source/Syscalls.c \
	Userland/API/Source/XMLParser.c \
	Userland/Service/Source/service_client.c \
	com.ImplusOS.netstack/Source/DNS/DNS.c

# Syscalls.c/service_client.c/DNS.c keep their OLD object-output paths
# (Userland/Syscalls.o, Userland/Service/service_client.o,
# Userland/Service/com.ImplusOS.netstack/DNS/DNS.o) since Userland/Source's
# AppCommon.mk (COMMON_OBJS) already hard-codes those -- only the compile
# rule's *source* side needs to point at the new location (see the pattern
# rules below).
USERLAND_INIT_OBJS := \
	$(patsubst Userland/Source/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/Source/%.c,$(USERLAND_C_SRCS))) \
	$(if $(filter Userland/Source/Syscalls.c,$(USERLAND_C_SRCS)),$(BUILD_DIR)/Userland/Syscalls.o) \
	$(if $(filter Userland/Service/Source/service_client.c,$(USERLAND_C_SRCS)),$(BUILD_DIR)/Userland/Service/service_client.o) \
	$(patsubst Userland/API/Source/%.c,$(BUILD_DIR)/Userland/API/%.o,$(filter Userland/API/Source/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst com.ImplusOS.netstack/Source/%.c,$(BUILD_DIR)/Userland/Service/com.ImplusOS.netstack/%.o,$(filter com.ImplusOS.netstack/Source/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst com.ImplusOS.posix/Source/%.c,$(BUILD_DIR)/Userland/Service/com.ImplusOS.posix/%.o,$(filter com.ImplusOS.posix/Source/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst libc/I_libc/Source/%.c,$(BUILD_DIR)/Userland/libc/I_libc/%.o,$(filter libc/I_libc/Source/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst Library/Source/%.c,$(BUILD_DIR)/Library/%.o,$(filter Library/Source/%.c,$(USERLAND_C_SRCS)))

USERLAND_APP_OBJS := \
	$(if $(filter Userland/Source/Syscalls.c,$(USERLAND_APP_C_SRCS)),$(BUILD_DIR)/Userland/Syscalls.o) \
	$(if $(filter Userland/Service/Source/service_client.c,$(USERLAND_APP_C_SRCS)),$(BUILD_DIR)/Userland/Service/service_client.o) \
	$(patsubst Userland/API/Source/%.c,$(BUILD_DIR)/Userland/API/%.o,$(filter Userland/API/Source/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst com.ImplusOS.netstack/Source/%.c,$(BUILD_DIR)/Userland/Service/com.ImplusOS.netstack/%.o,$(filter com.ImplusOS.netstack/Source/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst libc/I_libc/Source/%.c,$(BUILD_DIR)/Userland/libc/I_libc/%.o,$(filter libc/I_libc/Source/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst Library/Source/%.c,$(BUILD_DIR)/Library/%.o,$(filter Library/Source/%.c,$(USERLAND_APP_C_SRCS)))

RECOVERY_OBJS := \
	$(BUILD_DIR)/RecoveryEnvironment/Recovery.o \
	$(USERLAND_APP_OBJS)

USERLAND_CFLAGS := \
	-I. \
	-IKernel/Source/include \
	-Ilibc/I_libc/Source/include \
	-Icom.ImplusOS.posix/Source/include \
	-ILibrary/Source \
	-IVendor \
	-IVendor/Library/libjpeg/src \
	-I$(BUILD_DIR)/Vendor/Library/libjpeg/include \
	-IVendor/Library/freetype/include \
	-fno-stack-protector -ffreestanding -fno-pic -fno-builtin \
	$(USERLAND_ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
	-Os -g0 -ffunction-sections -fdata-sections -MMD -MP \
	$(EXTRA_USERLAND_CFLAGS)

USERLAND_CXXFLAGS := \
	-ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
	$(USERLAND_ARCH_CXXFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
	-fno-exceptions -fno-rtti \
	-Wall -Wextra -Os -g0 -ffunction-sections -fdata-sections -MMD -MP

USERLAND_LDFLAGS := -T Userland/Source/Userland.ld -nostdlib --build-id=none --gc-sections

all: $(BOOTLOADER_EFI) $(BOOTMANAGER_EFI) kernel vendor_libs app_build service_build driver_stage $(USERLAND_INIT_ELF)

vendor_libs:
	@$(MAKE) -C Vendor/Library ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) TOP_BUILD_DIR="$(abspath $(BUILD_DIR))"

# Stage the external Linux runtime (fetch pinned Debian .debs, extract .so
# closure, generate C.UTF-8) into $(LINUX_RUNTIME_STAGE). x86_64 only; on
# arm64 it is a no-op (Chromium/chrome is an x86_64 binary). The first run
# needs network; afterwards Vendor/LinuxRuntime/cache/ satisfies it offline.
# gtkdata: GTK3 demo runtime data;  xorgdata: Xorg + Mesa-DRI + xkb + fonts +
# xorg.conf for the Doom (Method A) X path.
linux_runtime_stage:
ifeq ($(ARCH),x86_64)
	@$(MAKE) -C $(LINUX_RUNTIME_DIR) stage gtkdata xorgdata locale \
		STAGE_DIR="$(abspath $(LINUX_RUNTIME_STAGE))" \
		CHROME_BIN="$(abspath Chromium/Resource/chrome)"
else
	@echo "linux_runtime_stage: skipped for ARCH=$(ARCH)"
endif

kernel:
	@$(MAKE) -C Kernel ARCH=$(ARCH) BUILD_DIR=$(abspath $(BUILD_DIR)) \
		EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS)"

# ── Userland services ─────────────────────────────────────────────
# Each Userland/Service/<name>/ builds one hot-loadable component
# (ldso, dynmain, posix, netstack, ...). Every service directory with a
# Makefile is built generically -- no per-service name is hard-coded.
service_build: vendor_libs $(USERLAND_INIT_OBJS)
	@set -e; \
	for dir in $(SERVICE_DIRS); do \
		$(MAKE) -C $$dir \
			ARCH=$(ARCH) \
			CROSS_COMPILE=$(CROSS_COMPILE) \
			TOP_BUILD_DIR="$(abspath $(BUILD_DIR))" \
			USERLAND_CFLAGS="$(USERLAND_CFLAGS)" \
			USERLAND_ARCH_CFLAGS="$(USERLAND_ARCH_CFLAGS)" \
			HOMEBREW_PREFIX="$(HOMEBREW_PREFIX)"; \
	done

app_build: vendor_libs $(USERLAND_INIT_OBJS)
	@set -e; \
	for dir in $(APP_DIRS); do \
			$(MAKE) -C $$dir \
				ARCH=$(ARCH) \
				CROSS_COMPILE=$(CROSS_COMPILE) \
				TOP_BUILD_DIR="$(abspath $(BUILD_DIR))" \
				USERLAND_CFLAGS="$(USERLAND_CFLAGS)" \
				USERLAND_ARCH_CFLAGS="$(USERLAND_ARCH_CFLAGS)" \
				HOMEBREW_PREFIX="$(HOMEBREW_PREFIX)"; \
		done

recovery_build: $(RECOVERY_INIT_ELF)

edk2_bootloader:
	@$(call EDK2_BUILD_MODULE,$(BOOTLOADER_DSC))

edk2_bootmanager:
	@$(call EDK2_BUILD_MODULE,$(BOOTMANAGER_DSC))

$(BOOTLOADER_EFI): edk2_bootloader
	@mkdir -p $(dir $@)
	@$(call COPY_EDK2_OUTPUT,$(BOOTLOADER_MODULE_NAME),$@)

$(BOOTMANAGER_EFI): edk2_bootmanager
	@mkdir -p $(dir $@)
	@$(call COPY_EDK2_OUTPUT,$(BOOTMANAGER_MODULE_NAME),$@)

$(BUILD_DIR)/Userland/%.o: Userland/Source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/Syscalls.o: Userland/Source/Syscalls.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/Service/service_client.o: Userland/Service/Source/service_client.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/API/%.o: Userland/API/Source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/Service/com.ImplusOS.netstack/%.o: com.ImplusOS.netstack/Source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/Service/com.ImplusOS.posix/%.o: com.ImplusOS.posix/Source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/libc/I_libc/%.o: libc/I_libc/Source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Library/%.o: Library/Source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/%.o: Userland/Source/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USERLAND_CXXFLAGS) -c $< -o $@

$(USERLAND_INIT_ELF): $(USERLAND_INIT_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@.tmp
	$(OBJCOPY) --strip-all -R .note -R .comment $@.tmp $@
	@rm -f $@.tmp

$(BUILD_DIR)/RecoveryEnvironment/%.o: $(RECOVERY_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) $(if $(filter 1,$(RECOVERY_AUDIO_TEST)),-DRECOVERY_AUDIO_TEST) \
		-IUserland/Source -IUserland/API/Source -c $< -o $@

$(RECOVERY_INIT_ELF): $(RECOVERY_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@.tmp
	$(OBJCOPY) --strip-all -R .note -R .comment $@.tmp $@
	@rm -f $@.tmp

driver_build:
	@set -e; \
	for dir in $(DRIVER_DIRS); do \
		$(MAKE) -C $$dir ARCH=$(ARCH); \
	done

driver_stage: driver_build
	@mkdir -p $(DRIVER_STAGE_DIR)
	@find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -delete
	@if [ -d $(DRIVER_BUILD_ROOT)/$(ARCH) ]; then \
		find $(DRIVER_BUILD_ROOT)/$(ARCH) -type f -name '*.ELF' \
			-exec cp {} $(DRIVER_STAGE_DIR)/ \; ; \
	fi

install_payload: all linux_runtime_stage
	@rm -rf $(INSTALL_PAYLOAD_ROOT)
	@mkdir -p \
		$(INSTALL_PAYLOAD_ROOT)/EFI/BOOT \
		$(INSTALL_PAYLOAD_ROOT)/Kernel/Driver \
		$(INSTALL_PAYLOAD_ROOT)/Kernel/Driver/OnDemand \
		$(INSTALL_PAYLOAD_ROOT)/Userland \
		$(INSTALL_PAYLOAD_ROOT)/Userland/Service \
		$(INSTALL_PAYLOAD_ROOT)/BootManager
	@cp $(BOOTLOADER_EFI)       $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/$(notdir $(BOOTLOADER_EFI))
	@cp $(BOOTMANAGER_EFI)      $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/BOOTMANAGER.EFI
	@cp $(KERNEL_ELF)           $(INSTALL_PAYLOAD_ROOT)/Kernel/Kernel_Main.ELF
	@cp $(USERLAND_INIT_ELF)    $(INSTALL_PAYLOAD_ROOT)/Userland/Userland.ELF
	@for dir in $(APP_DIRS); do \
		name=$$(basename "$$dir"); \
		cp -a "$(BUILD_DIR)/Userland/$$name" "$(INSTALL_PAYLOAD_ROOT)/Userland/"; \
	done
	@for dir in $(SERVICE_DIRS); do \
		name=$$(basename "$$dir"); \
		cp -a "$(BUILD_DIR)/Userland/Service/$$name" "$(INSTALL_PAYLOAD_ROOT)/Userland/Service/"; \
	done
	@[ -f Userland/Service/Source/services.list ] && cp Userland/Service/Source/services.list $(INSTALL_PAYLOAD_ROOT)/Userland/Service/ || true
	@cp -a $(BOOT_RESOURCE_DIR) $(INSTALL_PAYLOAD_ROOT)/BootManager/
	@if [ "$(ARCH)" = "x86_64" ]; then \
		cp -a $(LINUX_RUNTIME_STAGE)/lib64 $(INSTALL_PAYLOAD_ROOT)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/usr   $(INSTALL_PAYLOAD_ROOT)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/etc   $(INSTALL_PAYLOAD_ROOT)/; \
	fi
	@$(call STAGE_POSIX_BIN,$(INSTALL_PAYLOAD_ROOT))
	@$(call STAGE_DRIVER_ELFS,$(INSTALL_PAYLOAD_ROOT))
	@$(call STAGE_FIRMWARE,$(INSTALL_PAYLOAD_ROOT))
	@cp $(DRIVER_DB_SRC) $(INSTALL_PAYLOAD_ROOT)/Kernel/Driver/DriverDB.txt
	@find $(INSTALL_PAYLOAD_ROOT) -type f \( -name '*.o' -o -name '*.d' -o -name '*.a' \) -delete
	@mkdir -p $(INSTALL_PAYLOAD_DIR)
	@{ \
		echo "ImplusOS install payload"; \
		echo "Generated by make image"; \
		echo "Root archive: /Recovery/ImplusOS-root.tar.gz"; \
		echo "Install root entries:"; \
		find $(INSTALL_PAYLOAD_ROOT) -type f | sort | sed 's#^$(INSTALL_PAYLOAD_ROOT)/#/#'; \
	} > $(INSTALL_MANIFEST)
	@tar -C $(INSTALL_PAYLOAD_ROOT) -czf $(INSTALL_PAYLOAD_TGZ) .
	@rm -f $(INSTALL_DISK_IMAGE)
	@dd if=/dev/zero of=$(INSTALL_DISK_IMAGE) bs=1M count=$(INSTALL_DISK_IMAGE_SIZE_MB) status=none
	@mformat -i $(INSTALL_DISK_IMAGE) -F -v "IMPLUSOS" ::
	@PART_IMG=$(INSTALL_DISK_IMAGE); \
	mmd -i $$PART_IMG ::/EFI; \
	mmd -i $$PART_IMG ::/EFI/BOOT; \
	mmd -i $$PART_IMG ::/Kernel; \
	mmd -i $$PART_IMG ::/Kernel/Driver; \
	mmd -i $$PART_IMG ::/Kernel/Driver/OnDemand; \
	mmd -i $$PART_IMG ::/Kernel/Driver/Firmware; \
	mmd -i $$PART_IMG ::/Userland; \
	mmd -i $$PART_IMG ::/BootManager; \
	mmd -i $$PART_IMG ::/BootManager/Resource; \
	if [ "$(ARCH)" = "x86_64" ]; then mcopy -o -i $$PART_IMG $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTX64.EFI; fi; \
	if [ "$(ARCH)" = "arm64" ]; then mcopy -o -i $$PART_IMG $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTAA64.EFI; fi; \
	mcopy -o -i $$PART_IMG $(BOOTMANAGER_EFI) ::/EFI/BOOT/BOOTMANAGER.EFI; \
	mcopy -s -i $$PART_IMG $(BOOT_RESOURCE_DIR) ::/BootManager; \
	mcopy -o -i $$PART_IMG $(KERNEL_ELF) ::/Kernel/Kernel_Main.ELF; \
	mcopy -o -i $$PART_IMG $(USERLAND_INIT_ELF) ::/Userland/Userland.ELF; \
	mcopy -s -i $$PART_IMG $(INSTALL_PAYLOAD_ROOT)/Userland ::/Userland; \
	if [ "$(ARCH)" = "x86_64" ]; then \
		mcopy -s -o -i $$PART_IMG $(INSTALL_PAYLOAD_ROOT)/lib64 ::/; \
		mcopy -s -o -i $$PART_IMG $(INSTALL_PAYLOAD_ROOT)/usr ::/; \
	fi; \
	for f in $(DRIVER_STAGE_DIR)/*.ELF; do \
		[ -e "$$f" ] || continue; \
		base=$$(basename "$$f"); \
		case " $(ON_DEMAND_DRIVER_ELFS) " in \
			*" $$base "*) mcopy -o -i $$PART_IMG "$$f" ::/Kernel/Driver/OnDemand/;; \
			*) mcopy -o -i $$PART_IMG "$$f" ::/Kernel/Driver/;; \
		esac; \
	done; \
	$(call STAGE_FIRMWARE_FAT,$$PART_IMG) \
	mcopy -o -i $$PART_IMG $(DRIVER_DB_SRC) ::/Kernel/Driver/DriverDB.txt; \

image: install_payload recovery_build
	@mkdir -p $(IMAGE_DIR)
	@rm -f $(IMAGE) $(ESP_IMAGE)
	@dd if=/dev/zero of=$(ESP_IMAGE) bs=1M count=$(RECOVERY_ESP_IMAGE_SIZE_MB) status=none
	@mformat -i $(ESP_IMAGE) -v "IMPLUSOS" ::
	@mmd -i $(ESP_IMAGE) ::/EFI
	@mmd -i $(ESP_IMAGE) ::/EFI/BOOT
	@if [ "$(ARCH)" = "x86_64" ]; then \
		mcopy -o -i $(ESP_IMAGE) $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTX64.EFI; \
	else \
		mcopy -o -i $(ESP_IMAGE) $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTAA64.EFI; \
	fi
	@mcopy -o -i $(ESP_IMAGE) $(BOOTMANAGER_EFI) ::/EFI/BOOT/BOOTMANAGER.EFI
	@rm -rf $(IMAGE_STAGE_DIR)
	@mkdir -p \
		$(IMAGE_STAGE_DIR)/EFI/BOOT \
		$(IMAGE_STAGE_DIR)/Recovery \
		$(IMAGE_STAGE_DIR)/Kernel/Driver \
		$(IMAGE_STAGE_DIR)/Kernel/Driver/OnDemand \
		$(IMAGE_STAGE_DIR)/Userland \
		$(IMAGE_STAGE_DIR)/BootManager/Resource
	@cp $(ESP_IMAGE) $(IMAGE_STAGE_DIR)/esp.img
	@cp $(KERNEL_ELF) $(IMAGE_STAGE_DIR)/Kernel/Kernel_Main.ELF
	@cp $(RECOVERY_INIT_ELF) $(IMAGE_STAGE_DIR)/Userland/Userland.ELF
	@cp $(INSTALL_PAYLOAD_TGZ) $(IMAGE_STAGE_DIR)/Recovery/ImplusOS-root.tar.gz
	@cp $(INSTALL_DISK_IMAGE) $(IMAGE_STAGE_DIR)/Recovery/ImplusOS-install.img
	@cp $(INSTALL_MANIFEST) $(IMAGE_STAGE_DIR)/Recovery/MANIFEST.txt
	@cp -a $(BOOT_RESOURCE_DIR)/* $(IMAGE_STAGE_DIR)/BootManager/Resource/
	@if [ "$(ARCH)" = "x86_64" ]; then \
    	cp $(BOOTLOADER_EFI) $(IMAGE_STAGE_DIR)/EFI/BOOT/BOOTX64.EFI; \
	else \
		cp $(BOOTLOADER_EFI) $(IMAGE_STAGE_DIR)/EFI/BOOT/BOOTAA64.EFI; \
	fi

	@cp $(BOOTMANAGER_EFI) $(IMAGE_STAGE_DIR)/EFI/BOOT/BOOTMANAGER.EFI
	@$(call STAGE_DRIVER_ELFS,$(IMAGE_STAGE_DIR))
	@$(call STAGE_FIRMWARE,$(IMAGE_STAGE_DIR))
	@cp $(DRIVER_DB_SRC) $(IMAGE_STAGE_DIR)/Kernel/Driver/DriverDB.txt
	@xorriso -as mkisofs \
		-R \
		-J \
		-iso-level 3 \
		-V IMPLUSOS \
		-e esp.img \
		-no-emul-boot \
		-part_like_isohybrid \
		-isohybrid-gpt-basdat \
		-o $(IMAGE) \
		$(IMAGE_STAGE_DIR)

image_livecd: all linux_runtime_stage
	@mkdir -p $(IMAGE_DIR)
	@rm -f $(LIVECD_IMAGE) $(ESP_IMAGE)
	@dd if=/dev/zero of=$(ESP_IMAGE) bs=1M count=64 status=none
	@mformat -i $(ESP_IMAGE) -F -v "IMPLUSOS" ::
	@mmd -i $(ESP_IMAGE) ::/EFI
	@mmd -i $(ESP_IMAGE) ::/EFI/BOOT
	@mmd -i $(ESP_IMAGE) ::/BootManager
	@mmd -i $(ESP_IMAGE) ::/BootManager/Resource
	@if [ "$(ARCH)" = "x86_64" ]; then \
		mcopy -o -i $(ESP_IMAGE) $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTX64.EFI; \
	else \
		mcopy -o -i $(ESP_IMAGE) $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTAA64.EFI; \
	fi
	@mcopy -o -i $(ESP_IMAGE) $(BOOTMANAGER_EFI) ::/EFI/BOOT/BOOTMANAGER.EFI
	@mcopy -s -i $(ESP_IMAGE) $(BOOT_RESOURCE_DIR) ::/BootManager
	@rm -rf $(IMAGE_STAGE_DIR)
	@mkdir -p \
		$(IMAGE_STAGE_DIR)/EFI/BOOT \
		$(IMAGE_STAGE_DIR)/Kernel/Driver \
		$(IMAGE_STAGE_DIR)/Kernel/Driver/OnDemand \
		$(IMAGE_STAGE_DIR)/Userland \
		$(IMAGE_STAGE_DIR)/Userland \
		$(IMAGE_STAGE_DIR)/BootManager/Resource
	@cp $(ESP_IMAGE) $(IMAGE_STAGE_DIR)/esp.img
	@cp $(KERNEL_ELF) $(IMAGE_STAGE_DIR)/Kernel/Kernel_Main.ELF
	@cp $(USERLAND_INIT_ELF) $(IMAGE_STAGE_DIR)/Userland/Userland.ELF
	@for dir in $(APP_DIRS); do \
		name=$$(basename "$$dir"); \
		cp -a "$(BUILD_DIR)/Userland/$$name" "$(IMAGE_STAGE_DIR)/Userland/"; \
	done
	@mkdir -p $(IMAGE_STAGE_DIR)/Userland/Service
	@for dir in $(SERVICE_DIRS); do \
		name=$$(basename "$$dir"); \
		cp -a "$(BUILD_DIR)/Userland/Service/$$name" "$(IMAGE_STAGE_DIR)/Userland/Service/"; \
	done
	@[ -f Userland/Service/Source/services.list ] && cp Userland/Service/Source/services.list $(IMAGE_STAGE_DIR)/Userland/Service/ || true
	@if [ "$(ARCH)" = "x86_64" ]; then \
		cp -a $(LINUX_RUNTIME_STAGE)/lib64 $(IMAGE_STAGE_DIR)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/usr   $(IMAGE_STAGE_DIR)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/etc   $(IMAGE_STAGE_DIR)/; \
	fi
	@$(call STAGE_POSIX_BIN,$(IMAGE_STAGE_DIR))
	@cp -a $(BOOT_RESOURCE_DIR)/* $(IMAGE_STAGE_DIR)/BootManager/Resource/
	@if [ "$(ARCH)" = "x86_64" ]; then \
		cp $(BOOTLOADER_EFI) $(IMAGE_STAGE_DIR)/EFI/BOOT/BOOTX64.EFI; \
	else \
		cp $(BOOTLOADER_EFI) $(IMAGE_STAGE_DIR)/EFI/BOOT/BOOTAA64.EFI; \
	fi
	@cp $(BOOTMANAGER_EFI) $(IMAGE_STAGE_DIR)/EFI/BOOT/BOOTMANAGER.EFI
	@$(call STAGE_DRIVER_ELFS,$(IMAGE_STAGE_DIR))
	@$(call STAGE_FIRMWARE,$(IMAGE_STAGE_DIR))
	@cp $(DRIVER_DB_SRC) $(IMAGE_STAGE_DIR)/Kernel/Driver/DriverDB.txt
	@xorriso -as mkisofs \
		-R \
		-J \
		-iso-level 3 \
		-V IMPLUSOS \
		-e esp.img \
		-no-emul-boot \
		-part_like_isohybrid \
		-isohybrid-gpt-basdat \
		-o $(LIVECD_IMAGE) \
		$(IMAGE_STAGE_DIR)

ifeq ($(ARCH),arm64)
QEMU_MACHINE := virt
else
QEMU_MACHINE := pc
endif

QEMU_DISPLAY ?= none
QEMU_DISK_SIZE ?= 128M

QEMU_SYSTEM_AARCH64 ?= qemu-system-aarch64
QEMU_SYSTEM_X86_64 ?= qemu-system-x86_64
QEMU_INPUT_DEVICES := \
	-device qemu-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0
QEMU_NET_DEVICES ?= \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0

QEMU_COMMON := \
	-machine $(QEMU_MACHINE) \
	-smp 16,sockets=1,cores=4,threads=4 \
	-m 8192 \
	-device ich9-ahci,id=sata \
	$(QEMU_INPUT_DEVICES) \
	$(QEMU_NET_DEVICES) \
	-display $(QEMU_DISPLAY) \
	-serial stdio
	
QEMU_USB := \
	-drive if=none,id=usbstick,format=raw,file=$(LIVECD_IMAGE) \
	-device usb-storage,bus=xhci.0,drive=usbstick,bootindex=0

QEMU_DISK := \
	-drive if=none,id=cdrom0,file=$(LIVECD_IMAGE),media=cdrom,format=raw \
	-device ide-cd,drive=cdrom0,bus=sata.0,bootindex=0

qemu_disks:
	@[ -f disk1.qcow2 ] || qemu-img create -f qcow2 disk1.qcow2 $(QEMU_DISK_SIZE)
	@[ -f disk2.qcow2 ] || qemu-img create -f qcow2 disk2.qcow2 $(QEMU_DISK_SIZE)

qemu_input_status:
	@set +x; \
	echo "QEMU boot image: $(LIVECD_IMAGE)"; \
	echo "QEMU input devices: $(QEMU_INPUT_DEVICES)"; \
	echo "QEMU network devices: $(QEMU_NET_DEVICES)"

run_uefi_usb: qemu_input_status
	@if [ "$(ARCH)" = "arm64" ]; then \
		$(QEMU_SYSTEM_AARCH64) $(QEMU_COMMON) -bios $(QEMU_FIRMWARE) $(QEMU_USB); \
	else \
		$(QEMU_SYSTEM_X86_64) $(QEMU_COMMON) -drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) $(QEMU_USB); \
	fi

run_uefi_cdrom: qemu_input_status
	@if [ "$(ARCH)" = "arm64" ]; then \
		$(QEMU_SYSTEM_AARCH64) $(QEMU_COMMON) -bios $(QEMU_FIRMWARE) $(QEMU_DISK); \
	else \
		$(QEMU_SYSTEM_X86_64) $(QEMU_COMMON) -drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) $(QEMU_DISK); \
	fi

run_bios_cdrom:
	@if [ "$(ARCH)" = "arm64" ]; then \
		echo "There is no BIOS on arm64."; \
	else \
		qemu-system-x86_64 -cdrom $(LIVECD_IMAGE); \
	fi

clean:
	@rm -rf $(BUILD_ROOT) $(IMAGE_DIR)

-include $(USERLAND_INIT_OBJS:.o=.d)
-include $(USERLAND_APP_OBJS:.o=.d)
