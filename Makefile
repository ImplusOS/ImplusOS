SHELL := /bin/bash
.SHELLFLAGS := -euxo pipefail -c

export MTOOLSRC := /dev/null

.PHONY: all kernel run_uefi_usb run_uefi_cdrom run_bios_usb clean image app_build \
        driver_build driver_stage install_payload recovery_build

ARCH := x86_64
CC   = $(ARCH)-elf-gcc
CXX  = $(ARCH)-elf-g++
LD   = $(ARCH)-elf-ld
NASM = nasm

BUILD_DIR := Build
IMAGE_DIR := Image
IMAGE     := $(IMAGE_DIR)/ImplusOS.iso

OVMF_CODE := ./OVMF_CODE_4M.fd

KERNEL_DIR   := Kernel
USERLAND_DIR := Userland

KERNEL_ELF           := $(BUILD_DIR)/Kernel/Kernel_Main.ELF
BOOTX64_EFI          := $(BUILD_DIR)/Loader/BOOTX64.EFI
BOOTMANAGER_EFI      := $(BUILD_DIR)/BootManager/BOOTMANAGER.EFI
BIOS_STAGE1_BIN      := $(BUILD_DIR)/Loader/BIOS/stage1.bin
BIOS_STAGE2_BIN      := $(BUILD_DIR)/Loader/BIOS/stage2.bin
BIOS_BOOTMANAGER_BIN := $(BUILD_DIR)/BootManager/BootManager_BIOS.BIN
USERLAND_INIT_ELF    := $(BUILD_DIR)/Userland/Userland.ELF
RECOVERY_INIT_ELF    := $(BUILD_DIR)/RecoveryEnviroment/Userland.ELF
INSTALL_PAYLOAD_DIR  := $(BUILD_DIR)/InstallPayload
INSTALL_PAYLOAD_ROOT := $(INSTALL_PAYLOAD_DIR)/root
INSTALL_PAYLOAD_TGZ  := $(INSTALL_PAYLOAD_DIR)/ImplusOS-root.tar.gz
INSTALL_DISK_IMAGE   := $(INSTALL_PAYLOAD_DIR)/ImplusOS-install.img
INSTALL_MANIFEST     := $(INSTALL_PAYLOAD_DIR)/MANIFEST.txt

LOADER_CFLAGS := \
    -I. \
    -I/usr/local/include/efi/ \
    -I/usr/local/include/efi/$(ARCH) \
    -IBootManager/BootManager_libc/include \
    -ffreestanding -fpie -fshort-wchar -fno-stack-protector \
    -fno-builtin -mno-red-zone \
    -mno-sse2 -mno-sse3 -mno-ssse3 -mno-sse4 \
    -msoft-float \
    -Wall -Wextra -DEFI_FUNCTION_WRAPPER

BIOS_STAGE2_SECTORS := 32
BIOS_CFLAGS := \
	-m32 -I. -IKernel -IKernel/include -IBootManager/BIOS \
	-ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
	-mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
	-nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -O2

BIOS_LDFLAGS    := -m elf_i386 -nostdlib --build-id=none -T BootManager/BIOS/linker.ld
BIOS_BM_LDFLAGS := -m elf_i386 -nostdlib --build-id=none -T BootManager/BIOS/bootmanager_bios.ld

USERLAND_LDFLAGS     := -T Userland/Userland.ld -nostdlib --build-id=none
USERLAND_APP_LDFLAGS := -nostdlib --build-id=none
USERLAND_CFLAGS := \
    -Ilibc/include \
    -IUserland/POSIX/include \
	-IShareLib \
	-IThirdparty \
    -fno-stack-protector -ffreestanding -fno-pic -fno-builtin \
    -mcmodel=large -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
    -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
    -O3 -MMD -MP

USERLAND_CXXFLAGS := \
	-ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
	-mcmodel=large -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
	-fno-exceptions -fno-rtti \
	-Wall -Wextra -O3 -MMD -MP

DRIVER_MAKEFILES := $(shell find Kernel/Drivers/Server -name Makefile -print 2>/dev/null | sort)
DRIVER_DIRS      := $(sort $(patsubst %/,%,$(dir $(DRIVER_MAKEFILES))))
DRIVER_BUILD_ROOT := $(BUILD_DIR)/Modules
DRIVER_STAGE_DIR  := $(BUILD_DIR)/Kernel/Drivers

SHARELIB_C_SRCS := $(shell find ShareLib -name "*.c" 2>/dev/null)

USERLAND_C_SRCS := \
	libc/src/assert.c \
	libc/src/math.c \
	libc/src/stdlib.c \
	libc/src/string.c \
	libc/src/stdio.c \
	libc/src/errno.c \
	libc/src/posix.c \
	libc/src/sys/syscalls.c \
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
	libc/src/assert.c \
	libc/src/math.c \
	libc/src/stdlib.c \
	libc/src/string.c \
	libc/src/stdio.c \
	libc/src/errno.c \
	libc/src/posix.c \
	libc/src/sys/syscalls.c \
	$(SHARELIB_C_SRCS) \
	Userland/Syscalls.c \
	Userland/API/XMLParser.c \
	Userland/NetworkStack/DNS/DNS.c

USERLAND_INIT_OBJS := \
	$(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst libc/%.c,$(BUILD_DIR)/Userland/libc/%.o,$(filter libc/%.c,$(USERLAND_C_SRCS))) \
	$(patsubst ShareLib/%.c,$(BUILD_DIR)/ShareLib/%.o,$(filter ShareLib/%.c,$(USERLAND_C_SRCS)))
USERLAND_APP_OBJS := \
	$(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst libc/%.c,$(BUILD_DIR)/Userland/libc/%.o,$(filter libc/%.c,$(USERLAND_APP_C_SRCS))) \
	$(patsubst ShareLib/%.c,$(BUILD_DIR)/ShareLib/%.o,$(filter ShareLib/%.c,$(USERLAND_APP_C_SRCS)))

RECOVERY_OBJS := \
	$(BUILD_DIR)/RecoveryEnviroment/Recovery.o \
	$(USERLAND_APP_OBJS)

all: $(BOOTX64_EFI) \
     $(BOOTMANAGER_EFI) \
     $(BIOS_STAGE1_BIN) \
     $(BIOS_STAGE2_BIN) \
     $(BIOS_BOOTMANAGER_BIN) \
     kernel \
     $(USERLAND_INIT_ELF) \
     driver_stage \
     app_build

kernel:
	@$(MAKE) -C Kernel ARCH=$(ARCH) BUILD_DIR=$(abspath $(BUILD_DIR))

app_build: $(USERLAND_INIT_OBJS)
	@$(MAKE) -C Userland/Application/SystemApps/com_ImplusOS_windowmanager
	@$(MAKE) -C Userland/Application/SystemApps/com_ImplusOS_shell
	@$(MAKE) -C Userland/Application/SystemApps/com_ImplusOS_version
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_exampleApp
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_NetworkTest
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_editor
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_filemanager
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_procman
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_vm

recovery_build: $(RECOVERY_INIT_ELF)

install_payload: all
	@rm -rf $(INSTALL_PAYLOAD_ROOT)
	@mkdir -p \
		$(INSTALL_PAYLOAD_ROOT)/EFI/BOOT \
		$(INSTALL_PAYLOAD_ROOT)/Kernel/Driver \
		$(INSTALL_PAYLOAD_ROOT)/Userland \
		$(INSTALL_PAYLOAD_ROOT)/BootManager
	@cp $(BOOTX64_EFI)          $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/BOOTX64.EFI
	@cp $(BOOTMANAGER_EFI)      $(INSTALL_PAYLOAD_ROOT)/EFI/BOOT/BOOTMANAGER.EFI
	@cp $(BIOS_BOOTMANAGER_BIN) $(INSTALL_PAYLOAD_ROOT)/BootManager/BootManager_BIOS.BIN
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
	mcopy -o -i $$PART_IMG $(BOOTX64_EFI)          ::/EFI/BOOT/BOOTX64.EFI; \
	mcopy -o -i $$PART_IMG $(BOOTMANAGER_EFI)      ::/EFI/BOOT/BOOTMANAGER.EFI; \
	mcopy -o -i $$PART_IMG $(BIOS_BOOTMANAGER_BIN) ::/BootManager/BootManager_BIOS.BIN; \
	mcopy -o -i $$PART_IMG $(KERNEL_ELF)           ::/Kernel/Kernel_Main.ELF; \
	mcopy -o -i $$PART_IMG $(USERLAND_INIT_ELF)    ::/Userland/Userland.ELF; \
	mcopy -s -i $$PART_IMG $(BUILD_DIR)/Userland/SystemApps ::/Userland; \
	mcopy -s -i $$PART_IMG $(BUILD_DIR)/Userland/UserApps ::/Userland; \
	mcopy -s -i $$PART_IMG $(BOOT_RESOURCE_DIR) ::/BootManager; \
	for f in $(DRIVER_STAGE_DIR)/*.ELF; do \
		[ -e "$$f" ] || continue; \
		mcopy -o -i $$PART_IMG "$$f" ::/Kernel/Driver/; \
	done

driver_build:
	@set -e; \
	for dir in $(DRIVER_DIRS); do \
		$(MAKE) -C $$dir; \
	done

driver_stage: driver_build
	@mkdir -p $(DRIVER_STAGE_DIR)
	@find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -delete
	@if [ -d $(DRIVER_BUILD_ROOT) ]; then \
		find $(DRIVER_BUILD_ROOT) -type f -name '*.ELF' -exec cp {} $(DRIVER_STAGE_DIR)/ \; ; \
	fi

EFI_CC      := x86_64-w64-mingw32-gcc
EFI_LD      := x86_64-elf-ld
EFI_OBJCOPY := x86_64-elf-objcopy

$(BUILD_DIR)/Loader/Loader.o: BootLoader/x86_64/UEFI/Loader.c
	mkdir -p $(dir $@)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(BOOTX64_EFI): $(BUILD_DIR)/Loader/Loader.o
	mkdir -p $(dir $@)
	$(EFI_LD) -nostdlib \
		-znocombreloc \
		--defsym=_DYNAMIC=0 \
		-T /usr/local/lib/elf_$(ARCH)_efi.lds \
		-shared -Bsymbolic \
		/usr/local/lib/crt0-efi-$(ARCH).o \
		$< \
		/usr/local/lib/libefi.a \
		/usr/local/lib/libgnuefi.a \
		-o $@.so
	$(EFI_OBJCOPY) \
		-j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc \
		-j .rodata -j .rdata -j .rodata.* \
		-O efi-app-$(ARCH) \
		$@.so $@
	rm -f $@.so

$(BIOS_STAGE1_BIN): BootLoader/x86_64/BIOS/stage1.asm
	mkdir -p $(dir $@)
	$(NASM) -f bin -DSTAGE2_SECTORS=$(BIOS_STAGE2_SECTORS) $< -o $@

$(BUILD_DIR)/Loader/BIOS/lowlevel.o: BootLoader/x86_64/BIOS/lowlevel.asm BootManager/BIOS/stage2_constants.inc
	mkdir -p $(dir $@)
	$(NASM) -f elf32 -I. $< -o $@

BIOS_LOADER_OBJS := \
	$(BUILD_DIR)/Loader/BIOS/stage2_entry.o \
	$(BUILD_DIR)/Loader/BIOS/lowlevel.o \
	$(BUILD_DIR)/Loader/BIOS/BiosLoader.o \
	$(BUILD_DIR)/BootManager/BIOS/string.o

$(BUILD_DIR)/Loader/BIOS/stage2_entry.o: BootLoader/x86_64/BIOS/stage2_entry.asm BootManager/BIOS/stage2_constants.inc
	mkdir -p $(dir $@)
	$(NASM) -f elf32 -I. $< -o $@

$(BUILD_DIR)/Loader/BIOS/BiosLoader.o: BootLoader/x86_64/BIOS/BiosLoader.c
	mkdir -p $(dir $@)
	$(CC) $(BIOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/BootManager/BIOS/BootManager_BIOS.o: BootManager/BIOS/BootManager_BIOS.c BootManager/BIOS/BIOS_Handoff.h
	mkdir -p $(dir $@)
	$(CC) $(BIOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/BootManager/BIOS/string.o: BootManager/BootManager_libc/source/string.c
	mkdir -p $(dir $@)
	$(CC) $(BIOS_CFLAGS) -c $< -o $@

$(BIOS_STAGE2_BIN): $(BIOS_LOADER_OBJS) BootManager/BIOS/linker.ld
	mkdir -p $(dir $@)
	$(LD) $(BIOS_LDFLAGS) $(BIOS_LOADER_OBJS) -o $@.elf
	$(ARCH)-elf-objcopy -O binary $@.elf $@
	@size=$$(wc -c < $@); max=$$(( $(BIOS_STAGE2_SECTORS) * 512 )); \
	if [ $$size -gt $$max ]; then \
		echo "BIOS Loader too large: $$size bytes (max $$max)"; \
		exit 1; \
	fi; \
	truncate -s $$max $@

$(BUILD_DIR)/BootManager/BIOS/bm_entry.o: BootManager/BIOS/bm_entry.asm
	mkdir -p $(dir $@)
	$(NASM) -f elf32 $< -o $@

BIOS_BM_OBJS := \
	$(BUILD_DIR)/BootManager/BIOS/bm_entry.o \
	$(BUILD_DIR)/BootManager/BIOS/BootManager_BIOS.o \
	$(BUILD_DIR)/BootManager/BIOS/string.o

$(BIOS_BOOTMANAGER_BIN): $(BIOS_BM_OBJS) BootManager/BIOS/bootmanager_bios.ld
	mkdir -p $(dir $@)
	$(LD) $(BIOS_BM_LDFLAGS) $(BIOS_BM_OBJS) -o $@.elf
	$(ARCH)-elf-objcopy -O binary $@.elf $@

BOOTMANAGER_OBJS := \
	$(BUILD_DIR)/BootManager/UEFI/BootManager.o \
	$(BUILD_DIR)/BootManager/FAT32.o \
	$(BUILD_DIR)/BootManager/BootManager_libc/string.o \
	$(BUILD_DIR)/BootManager/BootManager_libc/stdlib.o

$(BUILD_DIR)/BootManager/UEFI/BootManager.o: BootManager/UEFI/BootManager_UEFI.c
	mkdir -p $(dir $@)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/BootManager/FAT32.o: BootManager/FAT32.c
	mkdir -p $(dir $@)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/BootManager/BootManager_libc/%.o: BootManager/BootManager_libc/source/%.c
	mkdir -p $(dir $@)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(BOOTMANAGER_EFI): $(BOOTMANAGER_OBJS)
	mkdir -p $(dir $@)
	$(ARCH)-elf-ld -nostdlib -znocombreloc --defsym=_DYNAMIC=0 \
		-T /usr/local/lib//elf_$(ARCH)_efi.lds \
		-shared -Bsymbolic \
		/usr/local/lib/crt0-efi-$(ARCH).o \
		$^ \
		/usr/local/lib/libefi.a \
		/usr/local/lib/libgnuefi.a \
		-o $@.so
	$(ARCH)-elf-objcopy -j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc -j .rodata -j .rdata -j .rodata.* \
		-O efi-app-$(ARCH) $@.so $@
	rm -f $@.so

$(BUILD_DIR)/Userland/%.o: Userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/libc/%.o: libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/ShareLib/%.o: ShareLib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/%.o: Userland/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USERLAND_CXXFLAGS) -c $< -o $@

$(USERLAND_INIT_ELF): $(USERLAND_INIT_OBJS)
	mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@

BOOT_RESOURCE_DIR := BootManager/Resource

$(BUILD_DIR)/RecoveryEnviroment/%.o: RecoveryEnviroment/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -IUserland -IUserland/API -c $< -o $@

$(RECOVERY_INIT_ELF): $(RECOVERY_OBJS)
	mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@

image: install_payload recovery_build
	@mkdir -p $(IMAGE_DIR)
	@rm -f $(IMAGE)

	@dd if=/dev/zero of=$(IMAGE) bs=1M count=128 status=none
	@mformat -i $(IMAGE) -F -v "IMPLUSOS" ::

	@PART_IMG=$(IMAGE); \
	mmd -i $$PART_IMG ::/EFI; \
	mmd -i $$PART_IMG ::/EFI/BOOT; \
	mmd -i $$PART_IMG ::/Kernel; \
	mmd -i $$PART_IMG ::/Kernel/Driver; \
	mmd -i $$PART_IMG ::/Userland; \
	mmd -i $$PART_IMG ::/Recovery; \
	mmd -i $$PART_IMG ::/BootManager; \
	mmd -i $$PART_IMG ::/BootManager/Resource; \
	\
	mcopy -o -i $$PART_IMG $(BOOTX64_EFI)          ::/EFI/BOOT/BOOTX64.EFI; \
	mcopy -o -i $$PART_IMG $(BOOTMANAGER_EFI)      ::/EFI/BOOT/BOOTMANAGER.EFI; \
	mcopy -o -i $$PART_IMG $(BIOS_BOOTMANAGER_BIN) ::/BootManager/BootManager_BIOS.BIN; \
	mcopy -o -i $$PART_IMG $(KERNEL_ELF)           ::/Kernel/Kernel_Main.ELF; \
	mcopy -o -i $$PART_IMG $(RECOVERY_INIT_ELF)    ::/Userland/Userland.ELF; \
	mcopy -o -i $$PART_IMG $(INSTALL_PAYLOAD_TGZ)  ::/Recovery/ImplusOS-root.tar.gz; \
	mcopy -o -i $$PART_IMG $(INSTALL_DISK_IMAGE)   ::/Recovery/ImplusOS-install.img; \
	mcopy -o -i $$PART_IMG $(INSTALL_MANIFEST)     ::/Recovery/MANIFEST.txt; \
	\
	mcopy -s -i $$PART_IMG $(BOOT_RESOURCE_DIR) ::/BootManager; \
	\
	for f in $(DRIVER_STAGE_DIR)/*.ELF; do \
		[ -e "$$f" ] || continue; \
		mcopy -o -i $$PART_IMG "$$f" ::/Kernel/Driver/; \
	done

	@echo "Creating hybrid ISO..."
	@mkdir -p $(IMAGE_DIR)/iso_root
	@cp $(IMAGE) $(IMAGE_DIR)/iso_root/boot.img

	@xorriso -as mkisofs \
		-o $(IMAGE_DIR)/ImplusOS.iso \
		-iso-level 3 \
		-full-iso9660-filenames \
		-J -joliet-long \
		-r \
		-volid "ImplusOS 0.2 Beta Clesk" \
		$(IMAGE_DIR)/iso_root

QEMU_COMMON = \
	-machine q35,accel=tcg \
	-cpu max \
	-smp 1 \
	-m 4G \
	-device qemu-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0 \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0 \
	-device ich9-ahci,id=sata \
	-drive if=pflash,format=raw,readonly=on,file=${OVMF_CODE} \
	-drive if=none,id=drive0,file=./disk.qcow2,format=qcow2 \
	-device ide-hd,drive=drive0,bus=sata.0 \
	-device ich9-intel-hda \
	-device hda-duplex \
	-rtc base=localtime \
	-serial stdio

QEMU_USB = \
	-drive if=none,id=usbstick,format=raw,file=${IMAGE} \
	-device usb-storage,bus=xhci.0,drive=usbstick

QEMU_DISK = \
	-drive file=${IMAGE},format=raw,if=none,id=disk0 \
	-device ide-hd,drive=disk0,bus=sata.0

run_uefi_usb:
	@qemu-system-$(ARCH) $(QEMU_COMMON) $(QEMU_USB)

run_uefi_cdrom:
	@qemu-system-$(ARCH) $(QEMU_COMMON) $(QEMU_DISK)

run_bios_usb:
	@qemu-system-$(ARCH) \
		-machine q35 \
		-smp 4,sockets=1,cores=4,threads=1 \
		-m 4G \
		-serial stdio \
		-device qemu-xhci,id=xhci \
		-drive if=none,id=usbstick,format=raw,file=$(IMAGE) \
		-device usb-storage,drive=usbstick

clean:
	@rm -rf $(BUILD_DIR) $(IMAGE_DIR)

-include $(USERLAND_INIT_OBJS:.o=.d)
-include $(USERLAND_APP_OBJS:.o=.d)
