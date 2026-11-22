# GNU Make >= 3.82 is required (.ONESHELL, used throughout). macOS ships GNU
# Make 3.81 as /usr/bin/make, which silently ignores .ONESHELL and then runs
# every recipe line in its own shell -- install a current one with
# `brew install make` and run it as `gmake`.
ifeq ($(filter oneshell,$(.FEATURES)),)
$(error GNU Make >= 3.82 is required (found $(MAKE_VERSION)). On macOS: brew install make, then run 'gmake')
endif

.ONESHELL:
SHELL := /bin/bash
.SHELLFLAGS := -euxo pipefail -c

export MTOOLSRC := /dev/null

.PHONY: all kernel app_build driver_build driver_stage recovery_build install_payload \
        image image_livecd qemu_input_status qemu_vars verify_usb \
        run_uefi_usb run_uefi_cdrom run_bios_cdrom \
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

ARCH ?= arm64

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

# Optical-media mastering. The install/live images are now UDF-bridge discs:
# an ISO9660 volume (kept for firmware/BootManager compatibility and as the
# kernel's fallback filesystem) that also carries a full UDF structure over
# the same files. The kernel's UDF driver
# (Kernel/Source/Drivers/FileSystem/UDF/) is preferred over ISO9660 for
# optical media and the running OS therefore reads its root from UDF.
#
# xorriso/libisofs cannot author UDF, so mastering uses genisoimage (cdrkit)
# with -udf. Install it with: sudo apt install -y genisoimage
# Override with ISO_MASTER=... if your genisoimage lives elsewhere.
#
# cdrkit is not packaged for macOS; there the Schily cdrtools mkisofs
# (`brew install cdrtools`) is used instead. It also writes UDF, but spells
# the EFI El Torito entry differently: -eltorito-platform efi -b <img>
# instead of cdrkit's -e <img>, and it has no -allow-limited-size (it needs
# none for files up to 4 GiB). Its default load size covers only part of a
# 64 MiB ESP, so the entry gets -boot-load-size 1: EDK2's PartitionDxe
# (ElTorito.c) treats a sector count below 2 as "the partition runs to the
# end of the disc", which exposes the whole FAT32 image. genisoimage is
# preferred when both are installed (on Debian, mkisofs is genisoimage).
ISO_MASTER ?= $(firstword $(shell command -v genisoimage 2>/dev/null) \
                          $(shell command -v mkisofs 2>/dev/null) genisoimage)
ifneq ($(findstring genisoimage,$(shell $(ISO_MASTER) -version 2>&1)),)
ISO_MASTER_FLAGS := -allow-limited-size
ISO_EFI_BOOT     := -eltorito-alt-boot -e esp.img -no-emul-boot
else
ISO_MASTER_FLAGS :=
ISO_EFI_BOOT     := -eltorito-platform efi -b esp.img -no-emul-boot -boot-load-size 1
endif

# $(call MASTER_UDF_ISO,<output-iso>,<staging-dir>) -- UEFI-only El Torito
# boot via the staged esp.img, Rock Ridge + UDF over the tree.
#
# No -J: BootManager reads only the ISO9660 Primary VD + Rock Ridge, the
# kernel reads UDF, so the Joliet volume is dead weight -- and dropping it
# keeps the ISO9660 descriptor set short enough that Tools/udf_promote.py
# can slide the UDF Volume Recognition Sequence to sector 18-20, where
# `file` (and OS probes) expect it on a bridge disc. Without that the image
# still carries a full UDF filesystem but keeps reporting as plain ISO9660.
#
# Then make the same file bootable from a USB stick (USB_HYBRID=1, default).
# El Torito only works from optical media: a UEFI firmware booting a stick
# ignores the boot catalog and looks for a GPT/MBR "EFI System" partition, so
# a bare ISO written with dd shows up as an empty, unbootable disk. The old
# `isohybrid --uefi` step could not fix that -- it needs an isolinux boot
# image, fails on this UEFI-only disc, and the failure was swallowed by
# `|| true`, so the image silently stayed non-USB-bootable.
#
# Tools/iso_hybrid_gpt.py writes a protective MBR + GPT into the ISO system
# area (sectors 0..33, unused by ISO9660), appends a copy of esp.img as a
# separate EFI System Partition plus the backup GPT after the volume, and
# verifies the result (FAT type vs. cluster count, GPT CRCs, no overlaps,
# El Torito EFI entry). Offsets inside the ISO/UDF volume do not move, so
# optical boot and the kernel's UDF/ISO9660 view are unchanged. The step is
# idempotent and fails the build on a broken layout; USB_HYBRID=0 skips it.
USB_HYBRID ?= 1
define MASTER_UDF_ISO
	$(ISO_MASTER) \
		-R -udf \
		-iso-level 3 \
		-input-charset utf-8 \
		$(ISO_MASTER_FLAGS) \
		-V IMPLUSOS \
		$(ISO_EFI_BOOT) \
		-o $(1) \
		$(2)
	python3 $(CURDIR)/Tools/udf_promote.py $(1)
	if [ "$(USB_HYBRID)" = "1" ]; then \
		python3 -I $(CURDIR)/Tools/iso_hybrid_gpt.py --esp-name esp.img $(1); \
	fi
endef

# External Linux runtime (glibc dynamic linker + .so closure + C.UTF-8) staged
# for external Linux-ABI binaries such as Chromium. The packages are x86_64;
# always bundled on x86_64 (Docs/Others/TODO_glibc_Port.md §7-3 -- no
# opt-out). On arm64 the same x86_64 runtime is bundled too, and its programs
# run under the translator (Userland/Application/Translator,
# Docs/Architecture/Compat_Layers.md §7); LINUX_RUNTIME_TRANSLATED=0 leaves
# it out of the arm64 image. Build rules live in Vendor/LinuxRuntime/Makefile;
# this file only delegates.
LINUX_RUNTIME_DIR   := Vendor/LinuxRuntime
LINUX_RUNTIME_TRANSLATED ?= 1
LINUX_RUNTIME_STAGE := $(BUILD_ROOT)/x86_64/LinuxRuntime/stage
ifeq ($(ARCH),x86_64)
LINUX_RUNTIME_SHIP := 1
else
LINUX_RUNTIME_SHIP := $(LINUX_RUNTIME_TRANSLATED)
endif
# Size of the LiveCD ESP (esp.img inside the ISO, and its copy on the USB
# GPT). It is formatted FAT32 (mformat -F), and UEFI FAT drivers decide the
# FAT type from the cluster count: below 65525 clusters (~33 MiB with
# 512-byte clusters) a "FAT32" volume is read as FAT16 and fails to mount.
# image_livecd refuses anything under 33 MiB; Tools/iso_hybrid_gpt.py checks
# the same thing on the finished image. (This variable used to be 16 and
# unused; image_livecd hard-coded 64, which is kept as the default.)
LIVECD_ESP_IMAGE_SIZE_MB ?= 64

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
# Every module Makefile names its output with DRIVER_NAME (module.mk turns it
# into Build/Modules/$(ARCH)/<name>/<name>.ELF). Collected here so driver_stage
# below can stage exactly the modules the source tree currently defines.
DRIVER_MODULE_NAMES := $(shell sed -n 's/^DRIVER_NAME[[:space:]]*:=[[:space:]]*//p' $(DRIVER_MAKEFILES) 2>/dev/null | sort -u)
DRIVER_BUILD_ROOT := $(BUILD_ROOT)/Modules
DRIVER_STAGE_DIR  := $(BUILD_DIR)/Kernel/Drivers

# Firmware blobs staged verbatim (not built) into Kernel/Driver/Firmware/ on
# the boot image (the boot image path convention is kept as-is; only the
# source-tree location moved -- see Docs/Others/TODO_OS_Refactor.md 4.). Every
# immediate subdirectory of FIRMWARE_SRC_DIR (e.g.
# Kernel/Drivers/Firmware/AIC8800/, see its README.md) is copied over as-is by
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

# Foreign (Linux-ABI) programs that shell out expect /bin/sh to exist. Xorg
# invokes xkbcomp through `/bin/sh -c`; the statically linked BusyBox is ET_EXEC
# at 0x400000, which collides with the kernel's low identity map. Use the PIE
# Debian dash staged by LinuxRuntime as /bin/sh, and keep BusyBox for applets.
# $(1) = image root.
BUSYBOX_BIN := Userland/Application/BusyBox/Resource/busybox

# What the Terminal app runs instead of a bare shell. `cat /dev/applog` streams
# the kernel's Linux-program launch log ([app] lines, see
# OS_CONFIG_FOREIGN_LAUNCH_LOG) into the window, and then the shell takes the
# foreground, so the window behaves like a Linux console: kernel messages
# appear over a usable prompt.
#
# /dev/applog and NOT /dev/kmsg: the whole kernel log includes a line per
# AF_UNIX packet and per page fault, and drawing those lines is itself X
# traffic that gets logged, so a window streaming /dev/kmsg feeds back on
# itself. Launch lines come only from exec and exit, so the loop cannot close.
# `cat /dev/kmsg` by hand is still there when the full log is wanted.
#
# Kept as a file on the image rather than an argument because
# process_spawn_with_arg() splits its argument on whitespace and has no
# quoting, so `sh -c '...'` cannot survive the trip.
# Programs init spawns once the session is up, one ELF path per line.
# Empty by default -- the file is only written when AUTOSTART is set, e.g.
#
#   make image_livecd AUTOSTART=/Userland/Chromium/Chromium.ELF
#
# which is how a headless QEMU run drives an app with nobody at the keyboard.
# $(1) = image root.
AUTOSTART ?=
define STAGE_AUTOSTART
	if [ -n "$(AUTOSTART)" ]; then \
		mkdir -p "$(1)/Userland"; \
		printf '%s\n' $(foreach p,$(AUTOSTART),'$(p)') > "$(1)/Userland/autostart.list"; \
	fi
endef

define STAGE_POSIX_SHELL
	if [ -f $(BUSYBOX_BIN) ]; then \
		mkdir -p "$(1)/bin"; \
		cp $(BUSYBOX_BIN) "$(1)/bin/busybox"; \
		if [ -f "$(1)/usr/bin/dash" ]; then \
			cp "$(1)/usr/bin/dash" "$(1)/bin/sh"; \
		else \
			cp $(BUSYBOX_BIN) "$(1)/bin/sh"; \
		fi; \
		printf '%s\n' \
			'#!/bin/sh' \
			'# ImplusOS terminal session: app launch log + shell.' \
			'/bin/busybox cat /dev/applog &' \
			'exec /bin/sh' > "$(1)/bin/imsession"; \
		chmod +x "$(1)/bin/imsession"; \
	fi
endef

# /usr/bin/xdg-open for Linux programs: the native forwarder built from
# Userland/Application/XdgOpen (see its Main.c). Chromium runs it to open a
# download or show it in its folder.
XDG_OPEN_ELF := $(BUILD_DIR)/Userland/XdgOpen/XdgOpen.ELF
define STAGE_XDG_OPEN
	if [ -f $(XDG_OPEN_ELF) ]; then \
		mkdir -p "$(1)/usr/bin"; \
		cp $(XDG_OPEN_ELF) "$(1)/usr/bin/xdg-open"; \
	fi
endef

# Environment files for Linux programs (Userland/API/Source/LinuxEnv.h):
# every Environment/*.env under Userland/API and under each application
# directory goes into $(1)/etc/ImplusOS/linux-env/. Generic over
# whatever files exist -- no program name is baked in here; two sources
# shipping the same file name is a build error rather than a silent override.
LINUX_ENV_FILES := $(wildcard Userland/API/Environment/*.env \
                              Userland/Application/*/Environment/*.env)
define STAGE_LINUX_ENV
	mkdir -p "$(1)/etc/ImplusOS/linux-env"; \
	for f in $(LINUX_ENV_FILES); do \
		dst="$(1)/etc/ImplusOS/linux-env/$$(basename "$$f")"; \
		if [ -e "$$dst" ] && ! cmp -s "$$f" "$$dst"; then \
			echo "error: $$(basename "$$f") is shipped more than once ($$f)" >&2; \
			exit 1; \
		fi; \
		cp "$$f" "$$dst"; \
	done
endef

# Copies every immediate subdirectory of $(FIRMWARE_SRC_DIR) (e.g. AIC8800/)
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

# Every immediate subdirectory of Userland/Application/ that carries its own
# Makefile is a buildable app -- no per-app name is hard-coded here. Drop in a
# new app directory with a Makefile and it is picked up automatically. A
# directory with a `.nobuild` marker file is skipped (e.g. an app still
# being ported off a removed dependency).
APP_DIRS := $(patsubst %/,%,$(filter-out $(dir $(wildcard Userland/Application/*/.nobuild)),$(dir $(wildcard Userland/Application/*/Makefile))))

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
	libc/I_libc/Source/src/resolv.c \
	libc/I_libc/Source/src/dlfcn.c \
	libc/I_libc/Source/src/sys/syscalls.c \
	libc/I_libc/Source/src/sys/$(ARCH)/hal_syscall.c \
	libc/I_libc/Source/src/sys/$(ARCH)/setjmp.c \
	$(LIBRARY_C_SRCS) \
	Userland/Source/Userland.c \
	Userland/Source/Syscalls.c

USERLAND_APP_C_SRCS := \
	libc/I_libc/Source/src/assert.c \
	libc/I_libc/Source/src/math.c \
	libc/I_libc/Source/src/stdlib.c \
	libc/I_libc/Source/src/string.c \
	libc/I_libc/Source/src/iconv.c \
	libc/I_libc/Source/src/stdio.c \
	libc/I_libc/Source/src/errno.c \
	libc/I_libc/Source/src/posix.c \
	libc/I_libc/Source/src/resolv.c \
	libc/I_libc/Source/src/dlfcn.c \
	libc/I_libc/Source/src/sys/syscalls.c \
	libc/I_libc/Source/src/sys/$(ARCH)/hal_syscall.c \
	libc/I_libc/Source/src/sys/$(ARCH)/setjmp.c \
	$(LIBRARY_C_SRCS) \
	Userland/Source/Syscalls.c \
	Userland/API/Source/Theme.c \
	Userland/API/Source/UI.c \
	Userland/API/Source/XSession.c \
	Userland/API/Source/X11Wire.c \
	Userland/API/Source/LinuxEnv.c

# Syscalls.c keeps its OLD object-output path (Userland/Syscalls.o) since
# Userland/Source's AppCommon.mk (COMMON_OBJS) already hard-codes it -- only
# the compile rule's *source* side needs to point at the new location (see
# the pattern rules below).
USERLAND_INIT_OBJS := \
	$(patsubst Userland/Source/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/Source/%.c,$(USERLAND_C_SRCS))) \
	$(if $(filter Userland/Source/Syscalls.c,$(USERLAND_C_SRCS)),$(BUILD_DIR)/Userland/Syscalls.o) \
	$(patsubst Userland/API/Source/%.c,$(BUILD_DIR)/Userland/API/%.o,$(filter Userland/API/Source/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst libc/I_libc/Source/%.c,$(BUILD_DIR)/Userland/libc/I_libc/%.o,$(filter libc/I_libc/Source/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst Library/Source/%.c,$(BUILD_DIR)/Library/%.o,$(filter Library/Source/%.c,$(USERLAND_C_SRCS)))

USERLAND_APP_OBJS := \
	$(if $(filter Userland/Source/Syscalls.c,$(USERLAND_APP_C_SRCS)),$(BUILD_DIR)/Userland/Syscalls.o) \
	$(patsubst Userland/API/Source/%.c,$(BUILD_DIR)/Userland/API/%.o,$(filter Userland/API/Source/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst libc/I_libc/Source/%.c,$(BUILD_DIR)/Userland/libc/I_libc/%.o,$(filter libc/I_libc/Source/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst Library/Source/%.c,$(BUILD_DIR)/Library/%.o,$(filter Library/Source/%.c,$(USERLAND_APP_C_SRCS)))

RECOVERY_OBJS := \
	$(BUILD_DIR)/RecoveryEnvironment/Recovery.o \
	$(USERLAND_APP_OBJS)

USERLAND_CFLAGS := \
	-I. \
	-IKernel/Source/include \
	-Ilibc/I_libc/Source/include \
	-ILibrary/Source \
	-IVendor \
	-IVendor/Library/freetype/include \
	-fno-stack-protector -ffreestanding -fno-pic -fno-builtin \
	$(USERLAND_ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
	-O2 -g0 -ffunction-sections -fdata-sections -MMD -MP \
	$(EXTRA_USERLAND_CFLAGS)

USERLAND_CXXFLAGS := \
	-ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
	$(USERLAND_ARCH_CXXFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
	-fno-exceptions -fno-rtti \
	-Wall -Wextra -O2 -g0 -ffunction-sections -fdata-sections -MMD -MP

USERLAND_LDFLAGS := -T Userland/Source/Userland.ld -nostdlib --build-id=none --gc-sections

# Appended after the objects. AArch64 needs libgcc for binary128 long double
# soft-fp (see Userland/Source/AppCommon.mk); x86_64 needs nothing.
ifeq ($(ARCH),arm64)
USERLAND_LIBS := $(shell $(CC) -print-libgcc-file-name)
else
USERLAND_LIBS :=
endif

all: $(BOOTLOADER_EFI) $(BOOTMANAGER_EFI) kernel vendor_libs app_build driver_stage $(USERLAND_INIT_ELF)

vendor_libs:
	@$(MAKE) -C Vendor/Library ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) TOP_BUILD_DIR="$(abspath $(BUILD_DIR))"

# Stage the external Linux runtime (fetch pinned Debian .debs, extract .so
# closure, generate C.UTF-8) into $(LINUX_RUNTIME_STAGE). Always the x86_64
# runtime: on arm64 it is staged for the translator (a no-op with
# LINUX_RUNTIME_TRANSLATED=0), into the same directory an x86_64 build uses.
# The first run needs network; afterwards Vendor/LinuxRuntime/cache/
# satisfies it offline.
# gtkdata: GTK3 demo runtime data;  xorgdata: Xorg + Mesa-DRI + xkb + fonts +
# xorg.conf for the Doom (Method A) X path;  fastfetchdata: /usr/bin/fastfetch
# plus its presets;  desktopdata: the .desktop entries and application icons;
# winedata: the x86_64 Wine loader, wineserver, builtin PE DLLs and fonts.
# every staged .deb carries, which is what the window manager builds its
# application list from.
# x11data: Xlib's locale data and the misc fonts' fonts.dir.
linux_runtime_stage:
ifeq ($(LINUX_RUNTIME_SHIP),1)
	@$(MAKE) -C $(LINUX_RUNTIME_DIR) stage gtkdata xorgdata fastfetchdata xtermdata x11data alsadata desktopdata winedata locale \
		STAGE_DIR="$(abspath $(LINUX_RUNTIME_STAGE))" \
		CHROME_BIN="$(abspath Userland/Application/Chromium/Resource/chrome)"
else
	@echo "linux_runtime_stage: skipped (LINUX_RUNTIME_TRANSLATED=0)"
endif

kernel:
	@$(MAKE) -C Kernel ARCH=$(ARCH) BUILD_DIR=$(abspath $(BUILD_DIR)) \
		EXTRA_KERNEL_CFLAGS="$(EXTRA_KERNEL_CFLAGS)"

app_build: vendor_libs $(USERLAND_INIT_OBJS) $(BUILD_DIR)/Userland/API/UI.o \
	$(BUILD_DIR)/Userland/API/XSession.o $(BUILD_DIR)/Userland/API/X11Wire.o \
	$(BUILD_DIR)/Userland/API/Theme.o \
	$(BUILD_DIR)/Userland/API/LinuxEnv.o
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

# Ordered after edk2_bootloader: both source edksetup.sh, which rewrites the
# shared $(EDK2_DIR)/Conf/BuildEnv.sh, and two concurrent writers under -j
# interleave it into a script that no longer parses.
edk2_bootmanager: | edk2_bootloader
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

$(BUILD_DIR)/Userland/API/%.o: Userland/API/Source/%.c
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
	$(LD) $(USERLAND_LDFLAGS) $^ $(USERLAND_LIBS) -o $@.tmp
	$(OBJCOPY) --strip-all -R .note -R .comment $@.tmp $@
	@rm -f $@.tmp

$(BUILD_DIR)/RecoveryEnvironment/%.o: $(RECOVERY_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) $(if $(filter 1,$(RECOVERY_AUDIO_TEST)),-DRECOVERY_AUDIO_TEST) \
		-IUserland/Source -IUserland/API/Source -c $< -o $@

$(RECOVERY_INIT_ELF): $(RECOVERY_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ $(USERLAND_LIBS) -o $@.tmp
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
	@# Stage one .ELF per module the source tree defines, by name -- not every
	@# .ELF found under $(DRIVER_BUILD_ROOT). Nothing ever removes a module's
	@# output directory, so a module deleted from the tree (as Wi-Fi/AX900 was
	@# when Wi-Fi/AIC8800 replaced it) would keep its last build staged, and
	@# the bootloader preloads everything staged here. A module missing from
	@# the build root is reported instead of silently skipped.
	@for name in $(DRIVER_MODULE_NAMES); do \
		elf=$(DRIVER_BUILD_ROOT)/$(ARCH)/$$name/$$name.ELF; \
		if [ -f "$$elf" ]; then \
			cp "$$elf" $(DRIVER_STAGE_DIR)/; \
		else \
			echo "driver_stage: $$elf missing (module did not build?)"; \
		fi; \
	done

install_payload: all linux_runtime_stage
	@rm -rf $(INSTALL_PAYLOAD_ROOT)
	@mkdir -p \
		$(INSTALL_PAYLOAD_ROOT)/EFI/BOOT \
		$(INSTALL_PAYLOAD_ROOT)/Kernel/Driver \
		$(INSTALL_PAYLOAD_ROOT)/Kernel/Driver/OnDemand \
		$(INSTALL_PAYLOAD_ROOT)/Userland \
		$(INSTALL_PAYLOAD_ROOT)/BootManager
	@cp $(BOOTLOADER_EFI)       $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/$(notdir $(BOOTLOADER_EFI))
	@cp $(BOOTMANAGER_EFI)      $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/BOOTMANAGER.EFI
	@cp $(KERNEL_ELF)           $(INSTALL_PAYLOAD_ROOT)/Kernel/Kernel_Main.ELF
	@cp $(USERLAND_INIT_ELF)    $(INSTALL_PAYLOAD_ROOT)/Userland/Userland.ELF
	@for dir in $(APP_DIRS); do \
		name=$$(basename "$$dir"); \
		cp -a "$(BUILD_DIR)/Userland/$$name" "$(INSTALL_PAYLOAD_ROOT)/Userland/"; \
	done
	@cp -a $(BOOT_RESOURCE_DIR) $(INSTALL_PAYLOAD_ROOT)/BootManager/
	@if [ "$(LINUX_RUNTIME_SHIP)" = "1" ]; then \
		cp -a $(LINUX_RUNTIME_STAGE)/lib64 $(INSTALL_PAYLOAD_ROOT)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/usr   $(INSTALL_PAYLOAD_ROOT)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/etc   $(INSTALL_PAYLOAD_ROOT)/; \
	fi
	@$(call STAGE_POSIX_SHELL,$(INSTALL_PAYLOAD_ROOT))
	@$(call STAGE_XDG_OPEN,$(INSTALL_PAYLOAD_ROOT))
	@$(call STAGE_LINUX_ENV,$(INSTALL_PAYLOAD_ROOT))
	@$(call STAGE_AUTOSTART,$(INSTALL_PAYLOAD_ROOT))
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
	@$(call MASTER_UDF_ISO,$(IMAGE),$(IMAGE_STAGE_DIR))

image_livecd: all linux_runtime_stage
	@[ "$(LIVECD_ESP_IMAGE_SIZE_MB)" -ge 33 ] || { \
		echo "LIVECD_ESP_IMAGE_SIZE_MB=$(LIVECD_ESP_IMAGE_SIZE_MB) is too small: a FAT32 ESP needs >= 33 MiB (see comment at LIVECD_ESP_IMAGE_SIZE_MB)"; \
		exit 1; }
	@mkdir -p $(IMAGE_DIR)
	@rm -f $(LIVECD_IMAGE) $(ESP_IMAGE)
	@dd if=/dev/zero of=$(ESP_IMAGE) bs=1M count=$(LIVECD_ESP_IMAGE_SIZE_MB) status=none
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
	@if [ "$(LINUX_RUNTIME_SHIP)" = "1" ]; then \
		cp -a $(LINUX_RUNTIME_STAGE)/lib64 $(IMAGE_STAGE_DIR)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/usr   $(IMAGE_STAGE_DIR)/; \
		cp -a $(LINUX_RUNTIME_STAGE)/etc   $(IMAGE_STAGE_DIR)/; \
	fi
	@$(call STAGE_POSIX_SHELL,$(IMAGE_STAGE_DIR))
	@$(call STAGE_XDG_OPEN,$(IMAGE_STAGE_DIR))
	@$(call STAGE_LINUX_ENV,$(IMAGE_STAGE_DIR))
	@$(call STAGE_AUTOSTART,$(IMAGE_STAGE_DIR))
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
	@$(call MASTER_UDF_ISO,$(LIVECD_IMAGE),$(IMAGE_STAGE_DIR))

# ?= so the machine type can be switched from the command line or the
# environment (e.g. QEMU_MACHINE=q35 to mimic a modern PCIe/UEFI PC).
ifeq ($(ARCH),arm64)
QEMU_MACHINE ?= virt
else
QEMU_MACHINE ?= pc
endif

# Image the run_* targets boot. Defaults to the LiveCD; to try the install
# media instead:
#   make run_uefi_usb QEMU_BOOT_IMAGE=Image/ImplusOS-x86_64-InstallMedia.iso
QEMU_BOOT_IMAGE ?= $(LIVECD_IMAGE)

# Writable UEFI variable store. Real firmware keeps NVRAM (boot entries,
# BootNext, anything BootManager stores); with only the read-only OVMF code
# image mapped, variable writes have nowhere to go and the boot behaves
# differently from hardware. If an OVMF_VARS template sits next to the
# Makefile (use the one matching OVMF_CODE_4M.fd) a per-build copy is mapped
# as the second pflash unit; without a template nothing changes.
QEMU_VARS_TEMPLATE ?= $(CURDIR)/OVMF_VARS_4M.fd
QEMU_VARS ?= $(BUILD_DIR)/OVMF_VARS.fd
QEMU_VARS_DRIVE :=
ifeq ($(ARCH),x86_64)
ifneq ($(wildcard $(QEMU_VARS_TEMPLATE)),)
QEMU_VARS_DRIVE := -drive if=pflash,format=raw,file=$(QEMU_VARS)
endif
endif

# macOS hosts: QEMU has no KVM, no gtk/sdl display and no virgl
# (virtio-gpu-gl) there, and OVMF is not something the repo ships -- Homebrew's
# qemu brings its own EDK2 builds, used when OVMF_CODE_4M.fd / AAVMF_CODE.fd
# are not next to the Makefile. Hypervisor.framework (hvf) only runs a guest
# of the host's own architecture; anything else falls back to TCG.
QEMU_FIRMWARE_FALLBACK := $(HOMEBREW_PREFIX)/share/qemu/edk2-$(if $(filter arm64,$(ARCH)),aarch64,x86_64)-code.fd
ifeq ($(wildcard $(QEMU_FIRMWARE)),)
ifneq ($(wildcard $(QEMU_FIRMWARE_FALLBACK)),)
QEMU_FIRMWARE := $(QEMU_FIRMWARE_FALLBACK)
endif
endif

ifeq ($(HOST_OS),Darwin)
QEMU_HOST_ARCH := $(if $(filter arm64 aarch64,$(UNAME_M)),arm64,x86_64)
ifeq ($(ARCH),$(QEMU_HOST_ARCH))
ifeq ($(ARCH),arm64)
# kernel-irqchip=off: newer QEMU drives Apple's in-kernel vGIC (hv_gic) by
# default under hvf, and with it the virtual timer interrupt never reaches
# the guest -- AAVMF itself stalls at "Start boot option" before any
# ImplusOS code runs. QEMU's own GICv3 emulation works. (A second -machine
# merges into the first, so QEMU_MACHINE is left as it is.)
QEMU_ACCEL ?= -accel hvf -cpu host -machine kernel-irqchip=off
else
QEMU_ACCEL ?= -accel hvf -cpu host
endif
else ifeq ($(ARCH),x86_64)
# -adx: with ADX in the guest's CPUID, BoringSSL's ADCX/ADOX code paths
# (bignum, X25519) come out wrong under TCG on this setup (QEMU 11.1 on
# Apple silicon), so every HTTPS connection Chromium makes fails
# (ERR_SSL_PROTOCOL_ERROR) while plain HTTP works. Without ADX it falls
# back to MULQ/ADC code, which TCG gets right. KVM is not affected.
QEMU_ACCEL ?= -accel tcg,thread=multi -cpu max,-adx
else
QEMU_ACCEL ?= -accel tcg,thread=multi -cpu max
endif
QEMU_DISPLAY ?= cocoa
QEMU_GPU ?= none
# Name the host backend explicitly: Core Audio is output-only in QEMU, so the
# hda-duplex codec used elsewhere logs "Can not open `adc' (no host audio
# driver)" at start-up, which reads as if sound were broken. hda-output is
# the same codec without the input pin.
QEMU_AUDIO_DEVICES ?= \
	-audiodev coreaudio,id=snd0 \
	-device ich9-intel-hda \
	-device hda-output,audiodev=snd0
else
QEMU_ACCEL ?= -cpu host -enable-kvm
endif

QEMU_DISPLAY ?= gtk
# Host-GPU (virgl) rendering for Mesa/Xorg/Chromium inside the guest:
#   make run_uefi_cdrom QEMU_GPU=virgl
# replaces the VGA with a virtio-gpu-gl device (the kernel drives it directly;
# no plain VGA is needed for the boot screen) and switches
# the display to an OpenGL-capable backend. The guest then reports
# SYSTEM_GRAPHICS_ACCEL_3D, Xorg's glamor and Chromium's GPU process run on
# the host GPU, and WebGL reports "ANGLE (Mesa, virgl (<host GPU>), ...)".
# sdl,gl=es is the backend that works under WSLg (gtk,gl=on aborts there with
# EGL_BAD_ACCESS once the virgl scanout is shown); override QEMU_DISPLAY if
# gtk,gl=on works on your host.
QEMU_GPU ?= virgl
ifeq ($(QEMU_GPU),virgl)
QEMU_DISPLAY := sdl,gl=es
QEMU_GPU_DEVICES := -vga none -device virtio-gpu-gl-pci
else ifeq ($(ARCH),arm64)
# The virt machine has no default display adapter: without one there is no
# GOP framebuffer and the boot manager has nothing to draw on. ramfb is the
# plain framebuffer AAVMF publishes as GOP and the kernel takes over as is.
QEMU_GPU_DEVICES := -device virtio-gpu-pci
else
QEMU_GPU_DEVICES :=
endif
QEMU_EXTRA ?= 
QEMU_DISK_SIZE ?= 128M

QEMU_SYSTEM_AARCH64 ?= qemu-system-aarch64
QEMU_SYSTEM_X86_64 ?= qemu-system-x86_64
# usb-tablet is an absolute pointer: the guest cursor follows the host cursor
# with no grab. With only a relative mouse (PS/2 or usb-mouse), QEMU's SDL
# backend grabs the pointer on click and warps it, which under WSLg shows up
# as the cursor jumping on click and moving backwards.
QEMU_INPUT_DEVICES := \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0
QEMU_NET_DEVICES ?= \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0
QEMU_AUDIO_DEVICES ?= \
	-device ich9-intel-hda \
	-device hda-duplex
QEMU_SMP ?= 2

QEMU_COMMON := \
	-machine $(QEMU_MACHINE) \
	$(QEMU_ACCEL) \
	-m 8192 \
	-smp $(QEMU_SMP) \
	-device ich9-ahci,id=sata \
	$(QEMU_INPUT_DEVICES) \
	$(QEMU_NET_DEVICES) \
	$(QEMU_AUDIO_DEVICES) \
	$(QEMU_GPU_DEVICES) \
	-display $(QEMU_DISPLAY) \
	-serial stdio \
	$(QEMU_EXTRA)
	
QEMU_USB := \
	-drive if=none,id=usbstick,format=raw,file=$(QEMU_BOOT_IMAGE) \
	-device usb-storage,bus=xhci.0,drive=usbstick,bootindex=0

ifeq ($(ARCH),arm64)
# AAVMF has no AHCI/ATAPI driver, so an ide-cd on the ICH9 AHCI never shows up
# as a boot option there (and the kernel has no virtio-scsi driver for a
# scsi-cd). virtio-blk is the read-only disc both of them can drive.
QEMU_DISK := \
	-drive if=none,id=cdrom0,file=$(QEMU_BOOT_IMAGE),format=raw,readonly=on \
	-device virtio-blk-pci,drive=cdrom0,bootindex=0
else
QEMU_DISK := \
	-drive if=none,id=cdrom0,file=$(QEMU_BOOT_IMAGE),media=cdrom,format=raw \
	-device ide-cd,drive=cdrom0,bus=sata.0,bootindex=0
endif

qemu_disks:
	@[ -f disk1.qcow2 ] || qemu-img create -f qcow2 disk1.qcow2 $(QEMU_DISK_SIZE)
	@[ -f disk2.qcow2 ] || qemu-img create -f qcow2 disk2.qcow2 $(QEMU_DISK_SIZE)

qemu_input_status:
	@set +x; \
	echo "QEMU boot image: $(QEMU_BOOT_IMAGE)"; \
	echo "QEMU input devices: $(QEMU_INPUT_DEVICES)"; \
	echo "QEMU network devices: $(QEMU_NET_DEVICES)"; \
	[ -f "$(QEMU_BOOT_IMAGE)" ] || { echo "Missing $(QEMU_BOOT_IMAGE) -- build it first (make image_livecd)"; exit 1; }

# OVMF stores boot and console device paths in the VARS file. Reused with a
# different device layout (e.g. after QEMU_GPU switched VGA for virtio-gpu),
# the firmware stalls on the TianoCore logo and never starts the boot
# manager. So the VARS file is tied to the layout that created it and
# recreated from the template whenever that layout changes.
QEMU_VARS_LAYOUT = $(QEMU_MACHINE) | $(strip $(QEMU_INPUT_DEVICES)) | $(strip $(QEMU_NET_DEVICES)) | $(strip $(QEMU_AUDIO_DEVICES)) | $(strip $(QEMU_GPU_DEVICES))

qemu_vars:
	@if [ -n "$(QEMU_VARS_DRIVE)" ]; then \
		layout='$(QEMU_VARS_LAYOUT)'; \
		if [ ! -f "$(QEMU_VARS)" ] || \
		   [ "$$(cat "$(QEMU_VARS).layout" 2>/dev/null)" != "$$layout" ]; then \
			[ -f "$(QEMU_VARS)" ] && echo "QEMU device layout changed: resetting $(QEMU_VARS)"; \
			mkdir -p "$(dir $(QEMU_VARS))"; \
			cp "$(QEMU_VARS_TEMPLATE)" "$(QEMU_VARS)"; \
			printf '%s\n' "$$layout" > "$(QEMU_VARS).layout"; \
		fi; \
	fi

verify_usb:
	@python3 -I $(CURDIR)/Tools/iso_hybrid_gpt.py --verify --esp-name esp.img $(QEMU_BOOT_IMAGE)

run_uefi_usb: qemu_input_status qemu_vars
	@if [ "$(ARCH)" = "arm64" ]; then \
		$(QEMU_SYSTEM_AARCH64) $(QEMU_COMMON) -bios $(QEMU_FIRMWARE) $(QEMU_USB); \
	else \
		$(QEMU_SYSTEM_X86_64) $(QEMU_COMMON) -drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) $(QEMU_VARS_DRIVE) $(QEMU_USB); \
	fi

run_uefi_cdrom: qemu_input_status qemu_vars
	@if [ "$(ARCH)" = "arm64" ]; then \
		$(QEMU_SYSTEM_AARCH64) $(QEMU_COMMON) -bios $(QEMU_FIRMWARE) $(QEMU_DISK); \
	else \
		$(QEMU_SYSTEM_X86_64) $(QEMU_COMMON) -drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) $(QEMU_VARS_DRIVE) $(QEMU_DISK); \
	fi

run_bios_cdrom:
	@if [ "$(ARCH)" = "arm64" ]; then \
		echo "There is no BIOS on arm64."; \
	else \
		qemu-system-x86_64 -cdrom $(QEMU_BOOT_IMAGE); \
	fi

clean:
	@rm -rf $(BUILD_ROOT) $(IMAGE_DIR)

-include $(USERLAND_INIT_OBJS:.o=.d)
-include $(USERLAND_APP_OBJS:.o=.d)