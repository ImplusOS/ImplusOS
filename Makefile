SHELL := /bin/bash
.SHELLFLAGS := -euxo pipefail -c

export MTOOLSRC := /dev/null

.PHONY: all kernel run_uefi_usb run_uefi_cdrom run_bios_usb clean image app_build \
        driver_build driver_stage copy_resources

ARCH := x86_64
CC   = $(ARCH)-elf-gcc
CXX  = $(ARCH)-elf-g++
LD   = $(ARCH)-elf-ld
NASM = nasm

BUILD_DIR := Build
IMAGE_DIR := Image
IMAGE     := $(IMAGE_DIR)/ImplusOS.img

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

IMAGE_SIZE_MB  := 256
PART_START_LBA := 2048
IMAGE_SIZE_SEC := $(shell echo "$$(( $(IMAGE_SIZE_MB) * 1024 * 1024 / 512 ))")
PART_END_LBA   := $(shell echo "$$(( $(IMAGE_SIZE_SEC) - 34 - 1 ))")

image: all
	@mkdir -p $(IMAGE_DIR)

	@rm -f $(IMAGE)
	@truncate -s $(IMAGE_SIZE_MB)M $(IMAGE)

	@sgdisk \
		--clear \
		--new=1:$(PART_START_LBA):$(PART_END_LBA) \
		--typecode=1:EF00 \
		--change-name=1:ImplusOS \
		$(IMAGE)

	@DEVICE=$$(hdiutil attach -imagekey diskimage-class=CRawDiskImage \
		-nomount $(IMAGE) | awk '/GUID_partition_scheme/ {print $$1}'); \
	\
	PARTITION=$${DEVICE}s1; \
	\
	echo "Device: $$DEVICE"; \
	echo "Partition: $$PARTITION"; \
	\
	sudo newfs_msdos -F 32 -v IMPLUSOS $$PARTITION; \
	\
	MNT_DIR=$$(mktemp -d); \
	sudo mount -t msdos $$PARTITION $$MNT_DIR; \
	\
	sudo mkdir -p $$MNT_DIR/EFI/BOOT; \
	sudo mkdir -p $$MNT_DIR/Kernel/Driver; \
	sudo mkdir -p $$MNT_DIR/Userland/SystemApps; \
	sudo mkdir -p $$MNT_DIR/Userland/UserApps; \
	sudo mkdir -p $$MNT_DIR/BootManager/Resource; \
	\
	sudo cp $(BOOTX64_EFI)          $$MNT_DIR/EFI/BOOT/BOOTX64.EFI; \
	sudo cp $(BOOTMANAGER_EFI)      $$MNT_DIR/EFI/BOOT/BOOTMANAGER.EFI; \
	sudo cp $(BIOS_BOOTMANAGER_BIN) $$MNT_DIR/BootManager/BootManager_BIOS.BIN; \
	sudo cp $(KERNEL_ELF)           $$MNT_DIR/Kernel/Kernel_Main.ELF; \
	sudo cp $(USERLAND_INIT_ELF)    $$MNT_DIR/Userland/Userland.ELF; \
	\
	if [ -d $(DRIVER_STAGE_DIR) ]; then \
		sudo find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' \
			-exec cp {} $$MNT_DIR/Kernel/Driver/ \; ; \
	fi; \
	\
	if [ -d $(BOOT_RESOURCE_DIR) ]; then \
		sudo cp -R $(BOOT_RESOURCE_DIR)/* \
			$$MNT_DIR/BootManager/Resource/ 2>/dev/null || true; \
	fi; \
	\
	if [ -d $(BUILD_DIR)/Userland/SystemApps ]; then \
		sudo cp -R $(BUILD_DIR)/Userland/SystemApps/* \
			$$MNT_DIR/Userland/SystemApps/ 2>/dev/null || true; \
	fi; \
	\
	if [ -d $(BUILD_DIR)/Userland/UserApps ]; then \
		sudo cp -R $(BUILD_DIR)/Userland/UserApps/* \
			$$MNT_DIR/Userland/UserApps/ 2>/dev/null || true; \
	fi; \
	\
	sync; \
	sudo umount $$MNT_DIR; \
	rmdir $$MNT_DIR; \
	hdiutil detach $$DEVICE

	@dd if=$(BIOS_STAGE1_BIN) of=$(IMAGE) bs=446 count=1 conv=notrunc 2>/dev/null

	@STAGE12=$$(mktemp); \
	cat $(BIOS_STAGE1_BIN) $(BIOS_STAGE2_BIN) > $$STAGE12; \
	dd if=$$STAGE12 of=$(IMAGE) bs=512 skip=1 seek=1 \
		count=$(BIOS_STAGE2_SECTORS) conv=notrunc 2>/dev/null; \
	rm -f $$STAGE12

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