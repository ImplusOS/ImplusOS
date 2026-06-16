.ONESHELL:
SHELL := /bin/bash
.SHELLFLAGS := -euxo pipefail -c

export MTOOLSRC := /dev/null

.PHONY: all kernel app_build driver_build driver_stage recovery_build install_payload \
        image run_uefi_usb run_uefi_cdrom qemu_disks clean \
        edk2_bootloader edk2_bootmanager

ARCH ?= x86_64

BUILD_ROOT ?= Build
BUILD_DIR ?= $(BUILD_ROOT)/$(ARCH)
IMAGE_DIR := Image
IMAGE := $(IMAGE_DIR)/ImplusOS-$(ARCH).iso

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
USERLAND_DIR := Userland
RECOVERY_DIR := RecoveryEnviroment

KERNEL_ELF        := $(BUILD_DIR)/Kernel/Kernel_Main.ELF
BOOTMANAGER_EFI   := $(BUILD_DIR)/BootManager/BOOTMANAGER.EFI
USERLAND_INIT_ELF := $(BUILD_DIR)/Userland/Userland.ELF
RECOVERY_INIT_ELF := $(BUILD_DIR)/RecoveryEnviroment/Userland.ELF

INSTALL_PAYLOAD_DIR  := $(BUILD_DIR)/InstallPayload
INSTALL_PAYLOAD_ROOT := $(INSTALL_PAYLOAD_DIR)/root
INSTALL_PAYLOAD_TGZ  := $(INSTALL_PAYLOAD_DIR)/ImplusOS-root.tar.gz
INSTALL_DISK_IMAGE   := $(INSTALL_PAYLOAD_DIR)/ImplusOS-install.img
INSTALL_MANIFEST     := $(INSTALL_PAYLOAD_DIR)/MANIFEST.txt

IMAGE_STAGE_DIR := $(BUILD_DIR)/ISO_ROOT
ESP_IMAGE       := $(IMAGE_DIR)/esp-$(ARCH).img

BOOT_RESOURCE_DIR := $(firstword $(wildcard BootManager/Resource))
ifeq ($(BOOT_RESOURCE_DIR),)
$(error BootManager resource directory not found. Expected BootManager/Resource.)
endif

BOOTLOADER_DSC := BootLoader/Configuration/ImplusOSBootLoader.dsc
BOOTMANAGER_DSC := BootManager/BootManager.dsc

EDK2_OUTPUT_ROOT := $(EDK2_DIR)/Build/$(EDK2_TARGET)_$(EDK2_TOOLCHAIN)/$(EDK2_ARCH)
BOOTLOADER_MODULE_NAME := ImplusOSBootLoader
BOOTMANAGER_MODULE_NAME := BootManager

define EDK2_BUILD_MODULE
	cd $(EDK2_DIR) && \
	. ./edksetup.sh && \
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

DRIVER_MAKEFILES := $(shell find Kernel/Drivers/Server -name Makefile -print 2>/dev/null | sort)
DRIVER_DIRS      := $(sort $(patsubst %/,%,$(dir $(DRIVER_MAKEFILES))))
DRIVER_BUILD_ROOT := $(BUILD_ROOT)/Modules
DRIVER_STAGE_DIR  := $(BUILD_DIR)/Kernel/Drivers

SHARELIB_C_SRCS := $(shell find ShareLib -name "*.c" 2>/dev/null)

USERLAND_C_SRCS := \
	libc/I_libc/src/assert.c \
	libc/I_libc/src/math.c \
	libc/I_libc/src/stdlib.c \
	libc/I_libc/src/string.c \
	libc/I_libc/src/stdio.c \
	libc/I_libc/src/errno.c \
	libc/I_libc/src/posix.c \
	libc/I_libc/src/sys/syscalls.c \
	libc/I_libc/src/sys/$(ARCH)/hal_syscall.c \
	$(SHARELIB_C_SRCS) \
	Userland/Userland.c \
	Userland/Syscalls.c \
	Userland/API/XMLParser.c \
	Userland/DriverFramework/API/DriverFrameworkAPI.c \
	Userland/NetworkStack/DNS/DNS.c \
	Userland/POSIX/src/posix_fdtable.c \
	Userland/POSIX/src/posix_file.c \
	Userland/POSIX/src/posix_process.c \
	Userland/POSIX/src/posix_signal.c \
	Userland/POSIX/src/posix_thread.c \
	Userland/POSIX/src/posix_net.c \
	Userland/POSIX/src/posix_time.c \
	Userland/POSIX/src/posix_mman.c \
	Userland/POSIX/src/posix_io.c

USERLAND_APP_C_SRCS := \
	libc/I_libc/src/assert.c \
	libc/I_libc/src/math.c \
	libc/I_libc/src/stdlib.c \
	libc/I_libc/src/string.c \
	libc/I_libc/src/stdio.c \
	libc/I_libc/src/errno.c \
	libc/I_libc/src/posix.c \
	libc/I_libc/src/sys/syscalls.c \
	libc/I_libc/src/sys/$(ARCH)/hal_syscall.c \
	$(SHARELIB_C_SRCS) \
	Userland/Syscalls.c \
	Userland/API/XMLParser.c \
	Userland/NetworkStack/DNS/DNS.c

USERLAND_INIT_OBJS := \
	$(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst libc/I_libc/%.c,$(BUILD_DIR)/Userland/libc/I_libc/%.o,$(filter libc/I_libc/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst ShareLib/%.c,$(BUILD_DIR)/ShareLib/%.o,$(filter ShareLib/%.c,$(USERLAND_C_SRCS)))

USERLAND_APP_OBJS := \
	$(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst libc/I_libc/%.c,$(BUILD_DIR)/Userland/libc/I_libc/%.o,$(filter libc/I_libc/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst ShareLib/%.c,$(BUILD_DIR)/ShareLib/%.o,$(filter ShareLib/%.c,$(USERLAND_APP_C_SRCS)))

RECOVERY_OBJS := \
	$(BUILD_DIR)/RecoveryEnviroment/Recovery.o \
	$(USERLAND_APP_OBJS)

USERLAND_CFLAGS := \
	-I. \
	-IKernel/include \
	-Ilibc/I_libc/include \
	-IUserland/POSIX/include \
	-IShareLib \
	-IThirdparty \
	-fno-stack-protector -ffreestanding -fno-pic -fno-builtin \
	$(USERLAND_ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
	-O3 -MMD -MP

USERLAND_CXXFLAGS := \
	-ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
	$(USERLAND_ARCH_CXXFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
	-fno-exceptions -fno-rtti \
	-Wall -Wextra -O3 -MMD -MP

USERLAND_LDFLAGS := -T Userland/Userland.ld -nostdlib --build-id=none

all: $(BOOTLOADER_EFI) $(BOOTMANAGER_EFI) kernel app_build driver_stage $(USERLAND_INIT_ELF)

kernel:
	@$(MAKE) -C Kernel ARCH=$(ARCH) BUILD_DIR=$(abspath $(BUILD_DIR))

app_build: $(USERLAND_INIT_OBJS)
	@set -e; \
	for dir in \
		Userland/Application/SystemApps/com_ImplusOS_windowmanager \
		Userland/Application/SystemApps/com_ImplusOS_shell \
		Userland/Application/SystemApps/com_ImplusOS_version \
		Userland/Application/UserApps/com_ImplusOS_exampleApp \
		Userland/Application/UserApps/com_ImplusOS_ImplusStore \
		Userland/Application/UserApps/com_ImplusOS_NetworkTest \
		Userland/Application/UserApps/com_ImplusOS_editor \
		Userland/Application/UserApps/com_ImplusOS_filemanager \
		Userland/Application/UserApps/com_ImplusOS_procman \
		Userland/Application/UserApps/com_ImplusOS_settings \
		Userland/Application/UserApps/com_ImplusOS_vm; do \
			$(MAKE) -C $$dir \
				ARCH=$(ARCH) \
				CROSS_COMPILE=$(CROSS_COMPILE) \
				TOP_BUILD_DIR="$(abspath $(BUILD_DIR))" \
				USERLAND_CFLAGS="$(USERLAND_CFLAGS)" \
				USERLAND_ARCH_CFLAGS="$(USERLAND_ARCH_CFLAGS)"; \
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

$(BUILD_DIR)/Userland/%.o: Userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/libc/I_libc/%.o: libc/I_libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/ShareLib/%.o: ShareLib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/%.o: Userland/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USERLAND_CXXFLAGS) -c $< -o $@

$(USERLAND_INIT_ELF): $(USERLAND_INIT_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@

$(BUILD_DIR)/RecoveryEnviroment/%.o: $(RECOVERY_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) $(if $(filter 1,$(RECOVERY_AUDIO_TEST)),-DRECOVERY_AUDIO_TEST) \
		-IUserland -IUserland/API -c $< -o $@

$(RECOVERY_INIT_ELF): $(RECOVERY_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@

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

install_payload: all
	@rm -rf $(INSTALL_PAYLOAD_ROOT)
	@mkdir -p \
		$(INSTALL_PAYLOAD_ROOT)/EFI/BOOT \
		$(INSTALL_PAYLOAD_ROOT)/Kernel/Driver \
		$(INSTALL_PAYLOAD_ROOT)/Userland \
		$(INSTALL_PAYLOAD_ROOT)/BootManager
	@cp $(BOOTLOADER_EFI)       $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/$(notdir $(BOOTLOADER_EFI))
	@cp $(BOOTMANAGER_EFI)      $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/BOOTMANAGER.EFI
	@cp $(KERNEL_ELF)           $(INSTALL_PAYLOAD_ROOT)/Kernel/Kernel_Main.ELF
	@cp $(USERLAND_INIT_ELF)    $(INSTALL_PAYLOAD_ROOT)/Userland/Userland.ELF
	@cp -a $(BUILD_DIR)/Userland/SystemApps $(INSTALL_PAYLOAD_ROOT)/Userland/
	@cp -a $(BUILD_DIR)/Userland/UserApps $(INSTALL_PAYLOAD_ROOT)/Userland/
	@cp -a $(BOOT_RESOURCE_DIR) $(INSTALL_PAYLOAD_ROOT)/BootManager/
	@for f in $(DRIVER_STAGE_DIR)/*.ELF; do \
		[ -e "$$f" ] || continue; \
		cp "$$f" $(INSTALL_PAYLOAD_ROOT)/Kernel/Driver/; \
	done
	@find $(INSTALL_PAYLOAD_ROOT) -type f \( -name '*.o' -o -name '*.d' \) -delete
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
	@dd if=/dev/zero of=$(INSTALL_DISK_IMAGE) bs=1M count=64 status=none
	@mformat -i $(INSTALL_DISK_IMAGE) -F -v "IMPLUSOS" ::
	@PART_IMG=$(INSTALL_DISK_IMAGE); \
	mmd -i $$PART_IMG ::/EFI; \
	mmd -i $$PART_IMG ::/EFI/BOOT; \
	mmd -i $$PART_IMG ::/Kernel; \
	mmd -i $$PART_IMG ::/Kernel/Driver; \
	mmd -i $$PART_IMG ::/Userland; \
	mmd -i $$PART_IMG ::/BootManager; \
	mmd -i $$PART_IMG ::/BootManager/Resource; \
	if [ "$(ARCH)" = "x86_64" ]; then mcopy -o -i $$PART_IMG $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTX64.EFI; fi; \
	if [ "$(ARCH)" = "arm64" ]; then mcopy -o -i $$PART_IMG $(BOOTLOADER_EFI) ::/EFI/BOOT/BOOTAA64.EFI; fi; \
	mcopy -o -i $$PART_IMG $(BOOTMANAGER_EFI) ::/EFI/BOOT/BOOTMANAGER.EFI; \
	mcopy -s -i $$PART_IMG $(BOOT_RESOURCE_DIR) ::/BootManager; \
	mcopy -o -i $$PART_IMG $(KERNEL_ELF) ::/Kernel/Kernel_Main.ELF; \
	mcopy -o -i $$PART_IMG $(USERLAND_INIT_ELF) ::/Userland/Userland.ELF; \
	mcopy -s -i $$PART_IMG $(BUILD_DIR)/Userland/SystemApps ::/Userland; \
	mcopy -s -i $$PART_IMG $(BUILD_DIR)/Userland/UserApps ::/Userland; \
	for f in $(DRIVER_STAGE_DIR)/*.ELF; do \
		[ -e "$$f" ] || continue; \
		mcopy -o -i $$PART_IMG "$$f" ::/Kernel/Driver/; \
	done

image: install_payload recovery_build
	@mkdir -p $(IMAGE_DIR)
	@rm -f $(IMAGE) $(ESP_IMAGE)
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
		$(IMAGE_STAGE_DIR)/Recovery \
		$(IMAGE_STAGE_DIR)/Kernel/Driver \
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
	@for f in $(DRIVER_STAGE_DIR)/*.ELF; do \
		[ -e "$$f" ] || continue; \
		cp "$$f" $(IMAGE_STAGE_DIR)/Kernel/Driver/; \
	done
	@xorriso -as mkisofs \
		-R \
		-J \
		-iso-level 3 \
		-V IMPLUSOS \
		-e esp.img \
		-no-emul-boot \
		-o $(IMAGE) \
		$(IMAGE_STAGE_DIR)

ifeq ($(ARCH),arm64)
QEMU_MACHINE := virt
else
QEMU_MACHINE := pc
endif

QEMU_DISPLAY ?= cocoa
QEMU_EXTRA ?=
QEMU_DISK_SIZE ?= 128M
QEMU_SYSTEM_AARCH64 ?= qemu-system-aarch64
QEMU_SYSTEM_X86_64 ?= qemu-system-x86_64

QEMU_COMMON := \
	-machine $(QEMU_MACHINE) \
	-cpu max \
	-smp 1 \
	-m 4G \
	-device qemu-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0 \
	-device ahci,id=ahci \
	-drive if=none,id=disk0,file=disk1.qcow2,format=qcow2 \
	-device ide-hd,bus=ahci.0,drive=disk0,serial=disk1 \
	-drive if=none,id=disk1,file=disk2.qcow2,format=qcow2 \
	-device ide-hd,bus=ahci.1,drive=disk1,serial=disk2 \
	-serial stdio \
	-device virtio-gpu-pci,max_outputs=2 \
	-nic user,model=virtio-net-pci \
	-display cocoa \
	$(QEMU_EXTRA)
	
QEMU_USB := \
	-drive if=none,id=usbstick,format=raw,file=$(IMAGE) \
	-device usb-storage,bus=xhci.0,drive=usbstick

QEMU_DISK := \
	-drive file=$(IMAGE),media=cdrom,format=raw,index=0,readonly=on \
	-drive if=none,id=cdrom0,file=$(IMAGE),media=cdrom,format=raw \
	-device ide-cd,drive=cdrom0,bus=ahci.2

qemu_disks:
	@[ -f disk1.qcow2 ] || qemu-img create -f qcow2 disk1.qcow2 $(QEMU_DISK_SIZE)
	@[ -f disk2.qcow2 ] || qemu-img create -f qcow2 disk2.qcow2 $(QEMU_DISK_SIZE)

run_uefi_usb:
	@if [ "$(ARCH)" = "arm64" ]; then \
		qemu-system-aarch64 $(QEMU_COMMON) -bios $(QEMU_FIRMWARE) $(QEMU_USB); \
	else \
		qemu-system-x86_64 $(QEMU_COMMON) -drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) $(QEMU_USB); \
	fi

run_uefi_cdrom:
	@if [ "$(ARCH)" = "arm64" ]; then \
		qemu-system-aarch64 $(QEMU_COMMON) -bios $(QEMU_FIRMWARE) $(QEMU_DISK); \
	else \
		qemu-system-x86_64 $(QEMU_COMMON) -drive if=pflash,format=raw,readonly=on,file=$(QEMU_FIRMWARE) $(QEMU_DISK); \
	fi

clean:
	@rm -rf $(BUILD_DIR) $(IMAGE_DIR)

-include $(USERLAND_INIT_OBJS:.o=.d)
-include $(USERLAND_APP_OBJS:.o=.d)
