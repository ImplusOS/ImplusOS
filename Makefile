.PHONY: all kernel run_usb run_ide run_bios clean image app_build driver_build driver_stage image_bios

ARCH := x86_64
CC   = x86_64-elf-gcc
CXX  = x86_64-elf-g++
LD   = x86_64-elf-ld
NASM = nasm

BUILD_DIR := Build
IMAGE_DIR := Image
IMAGE     := $(IMAGE_DIR)/ImplusOS.iso
BIOS_IMAGE := $(IMAGE_DIR)/ImplusOS-bios.img

OVMF_CODE := ./OVMF_CODE_4M.fd
OVMF_VARS := ./OVMF_VARS_4M.fd
DISK_IMG  := disk.qcow2

KERNEL_DIR   := Kernel
USERLAND_DIR := Userland

KERNEL_ELF        := $(BUILD_DIR)/Kernel/Kernel_Main.ELF
BOOTX64_EFI       := $(BUILD_DIR)/Loader/BOOTX64.EFI
BOOTMANAGER_EFI   := $(BUILD_DIR)/BootManager/BOOTMANAGER.EFI
BIOS_STAGE1_BIN   := $(BUILD_DIR)/Loader/BIOS/stage1.bin
BIOS_STAGE2_BIN   := $(BUILD_DIR)/Loader/BIOS/stage2.bin
USERLAND_INIT_ELF := $(BUILD_DIR)/Userland/Userland.ELF

LOADER_CFLAGS := \
	-I. \
	-I/usr/include/efi \
	-I/usr/include/efi/x86_64 \
	-I/usr/include/efi/protocol \
	-Ilibc/include \
	-IBootManager/BootManager_libc/include \
	-ffreestanding -fpic -fshort-wchar -fno-stack-protector \
	-fno-builtin -mno-red-zone \
	-Wall -Wextra -DEFI_FUNCTION_WRAPPER

BIOS_STAGE2_SECTORS := 127
BIOS_CFLAGS := \
	-m32 -I. -IKernel -IKernel/include -IBootManager/BIOS \
	-ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
	-mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
	-nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -O2

BIOS_LDFLAGS := -m elf_i386 -nostdlib --build-id=none -T BootManager/BIOS/linker.ld

USERLAND_LDFLAGS     := -T Userland/Userland.ld -nostdlib --build-id=none
USERLAND_APP_LDFLAGS := -nostdlib --build-id=none
USERLAND_CFLAGS := \
    -Ilibc/include \
    -IUserland/POSIX/include \
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
DRIVER_DIRS := $(sort $(patsubst %/,%,$(dir $(DRIVER_MAKEFILES))))
DRIVER_BUILD_ROOT := $(BUILD_DIR)/Modules
DRIVER_STAGE_DIR := $(BUILD_DIR)/Kernel/Drivers

USERLAND_C_SRCS := \
	libc/src/assert.c \
	libc/src/math.c \
	libc/src/stdlib.c \
	libc/src/string.c \
	libc/src/stdio.c \
	libc/src/errno.c \
	libc/src/posix.c \
	libc/src/sys/syscalls.c \
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
	Userland/Syscalls.c \
	Userland/API/XMLParser.c \
	Userland/NetworkStack/DNS/DNS.c

USERLAND_INIT_OBJS := $(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_C_SRCS))) \
                      $(patsubst libc/%.c,$(BUILD_DIR)/Userland/libc/%.o,$(filter libc/%.c,$(USERLAND_C_SRCS)))
USERLAND_APP_OBJS  := $(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_APP_C_SRCS))) \
                      $(patsubst libc/%.c,$(BUILD_DIR)/Userland/libc/%.o,$(filter libc/%.c,$(USERLAND_APP_C_SRCS)))

all: $(BOOTX64_EFI) \
     $(BOOTMANAGER_EFI) \
     $(BIOS_STAGE1_BIN) \
     $(BIOS_STAGE2_BIN) \
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
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_clock
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

$(BUILD_DIR)/Loader/Loader.o: BootLoader/x86_64/UEFI/Loader.c
	mkdir -p $(dir $@)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(BOOTX64_EFI): $(BUILD_DIR)/Loader/Loader.o
	mkdir -p $(dir $@)
	ld -nostdlib -znocombreloc \
		-T /usr/lib/elf_x86_64_efi.lds \
		-shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o \
		$< \
		/usr/lib/libefi.a \
		/usr/lib/libgnuefi.a \
		-o $@.so
	objcopy -j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc -j .rodata -j .rdata -j .rodata.* \
		-O efi-app-x86_64 $@.so $@
	rm -f $@.so

$(BIOS_STAGE1_BIN): BootLoader/x86_64/BIOS/stage1.asm
	mkdir -p $(dir $@)
	$(NASM) -f bin -DSTAGE2_SECTORS=$(BIOS_STAGE2_SECTORS) $< -o $@

BIOS_STAGE2_OBJS := \
	$(BUILD_DIR)/Loader/BIOS/stage2_entry.o \
	$(BUILD_DIR)/BootManager/BIOS/BootManager_BIOS.o \
	$(BUILD_DIR)/BootManager/BIOS/string.o

$(BUILD_DIR)/Loader/BIOS/stage2_entry.o: BootLoader/x86_64/BIOS/stage2_entry.asm BootManager/BIOS/stage2_constants.inc
	mkdir -p $(dir $@)
	$(NASM) -f elf32 -I. $< -o $@

$(BUILD_DIR)/BootManager/BIOS/BootManager_BIOS.o: BootManager/BIOS/BootManager_BIOS.c BootManager/BIOS/BIOS_Handoff.h
	mkdir -p $(dir $@)
	$(CC) $(BIOS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/BootManager/BIOS/string.o: BootManager/BootManager_libc/source/string.c
	mkdir -p $(dir $@)
	$(CC) $(BIOS_CFLAGS) -c $< -o $@

$(BIOS_STAGE2_BIN): $(BIOS_STAGE2_OBJS) BootManager/BIOS/linker.ld
	mkdir -p $(dir $@)
	$(LD) $(BIOS_LDFLAGS) $(BIOS_STAGE2_OBJS) -o $@.elf
	objcopy -O binary $@.elf $@
	@size=$$(wc -c < $@); max=$$(( $(BIOS_STAGE2_SECTORS) * 512 )); \
	if [ $$size -gt $$max ]; then \
		echo "BIOS stage2 too large: $$size > $$max bytes"; \
		exit 1; \
	fi; \
	truncate -s $$max $@
	# rm -f $@.elf

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
	ld -nostdlib -znocombreloc \
		-T /usr/lib/elf_x86_64_efi.lds \
		-shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o \
		$^ \
		/usr/lib/libefi.a \
		/usr/lib/libgnuefi.a \
		-o $@.so
	objcopy -j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc -j .rodata -j .rdata -j .rodata.* \
		-O efi-app-x86_64 $@.so $@
	rm -f $@.so

$(BUILD_DIR)/Userland/%.o: Userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/libc/%.o: libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/%.o: Userland/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USERLAND_CXXFLAGS) -c $< -o $@

$(USERLAND_INIT_ELF): $(USERLAND_INIT_OBJS)
	mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@

USERLAND_ELFS := $(shell find $(BUILD_DIR)/Userland -name '*.ELF' 2>/dev/null)
BOOT_RESOURCE_DIR := BootManager/Resource

__mount_image:
	@MOUNT_POINT=$$(mktemp -d); \
	LOOP_DEVICE=$$(sudo losetup -Pf --show $(IMAGE)) || exit 1; \
	trap "sudo losetup -d $$LOOP_DEVICE 2>/dev/null; rmdir $$MOUNT_POINT 2>/dev/null" EXIT; \
	sudo mkfs.fat -F32 $${LOOP_DEVICE}p1 || exit 1; \
	sudo mount $${LOOP_DEVICE}p1 $$MOUNT_POINT || exit 1; \
	sudo mkdir -p $$MOUNT_POINT/EFI/BOOT $$MOUNT_POINT/Kernel/Driver $$MOUNT_POINT/Userland $$MOUNT_POINT/BootManager; \
	sudo cp $(BOOTX64_EFI) $$MOUNT_POINT/EFI/BOOT/BOOTX64.EFI; \
	sudo cp $(BOOTMANAGER_EFI) $$MOUNT_POINT/EFI/BOOT/BOOTMANAGER.EFI; \
	sudo cp $(KERNEL_ELF) $$MOUNT_POINT/Kernel/Kernel_Main.ELF; \
	sudo cp $(USERLAND_INIT_ELF) $$MOUNT_POINT/Userland/Userland.ELF; \
	if [ -d $(DRIVER_STAGE_DIR) ]; then \
		find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -exec sudo cp {} $$MOUNT_POINT/Kernel/Driver/ \; ; \
	fi; \
	if [ -d $(BOOT_RESOURCE_DIR) ]; then \
		sudo cp -r $(BOOT_RESOURCE_DIR) $$MOUNT_POINT/BootManager/; \
	fi; \
	sync; \
	sudo umount $$MOUNT_POINT || exit 1;

ISO_ROOT := $(IMAGE_DIR)/iso_root
ESP_IMG  := $(IMAGE_DIR)/esp.iso

image_esp: all
	@mkdir -p $(ISO_ROOT)/EFI/BOOT $(ISO_ROOT)/Kernel/Driver
	@mkdir -p $(ISO_ROOT)/Userland
	@mkdir -p $(ISO_ROOT)/Userland/SystemApps
	@mkdir -p $(ISO_ROOT)/Userland/UserApps
	@cp $(BOOTX64_EFI) $(ISO_ROOT)/EFI/BOOT/BOOTX64.EFI
	@cp $(BOOTMANAGER_EFI) $(ISO_ROOT)/EFI/BOOT/BOOTMANAGER.EFI
	@cp $(KERNEL_ELF) $(ISO_ROOT)/Kernel/Kernel_Main.ELF
	@cp $(USERLAND_INIT_ELF) $(ISO_ROOT)/Userland/Userland.ELF
	@find $(ISO_ROOT)/Kernel/Driver -maxdepth 1 -type f -name '*.ELF' -delete
	@if [ -d $(DRIVER_STAGE_DIR) ]; then \
		find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -exec cp {} $(ISO_ROOT)/Kernel/Driver/ \; ; \
	fi
	@$(MAKE) __create_esp_iso
	@cp $(ESP_IMG) $(ISO_ROOT)/esp.iso
	@rsync -a $(BUILD_DIR)/Userland/SystemApps/ $(ISO_ROOT)/Userland/SystemApps/
	@rsync -a $(BUILD_DIR)/Userland/UserApps/ $(ISO_ROOT)/Userland/UserApps/
	@mkdir -p $(ISO_ROOT)/BootManager
	@rsync -a $(BOOT_RESOURCE_DIR) $(ISO_ROOT)/BootManager/
	@xorriso -as mkisofs -R -J -V "ImplusOS Clesk 0.2-beta" \
		-o $(IMAGE) \
		-eltorito-alt-boot -e esp.iso -no-emul-boot \
		$(ISO_ROOT)

	@cp $(IMAGE) Qemu/Test/Resource/ImplusOS.iso

image_bios: all
	@mkdir -p $(IMAGE_DIR)
	@dd if=/dev/zero of=$(BIOS_IMAGE) bs=1M count=128 2>/dev/null
	@printf 'label: dos\nunit: sectors\n\nstart=2048, type=c, bootable\n' | sfdisk $(BIOS_IMAGE) >/dev/null
	@dd if=$(BIOS_STAGE1_BIN) of=$(BIOS_IMAGE) bs=446 count=1 conv=notrunc 2>/dev/null
	@dd if=$(BIOS_STAGE2_BIN) of=$(BIOS_IMAGE) bs=512 seek=1 conv=notrunc 2>/dev/null
	@mformat -i $(BIOS_IMAGE)@@1048576 -F ::
	@mmd -i $(BIOS_IMAGE)@@1048576 ::/Kernel ::/Kernel/Driver ::/Userland ::/BootManager
	@mcopy -i $(BIOS_IMAGE)@@1048576 $(KERNEL_ELF) ::/Kernel/Kernel_Main.ELF
	@mcopy -i $(BIOS_IMAGE)@@1048576 $(USERLAND_INIT_ELF) ::/Userland/Userland.ELF
	@if [ -d $(BOOT_RESOURCE_DIR) ]; then \
		mcopy -i $(BIOS_IMAGE)@@1048576 -s $(BOOT_RESOURCE_DIR) ::/BootManager/; \
	fi
	if [ -d $(DRIVER_STAGE_DIR) ]; then \
		find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -exec mcopy -i $(BIOS_IMAGE)@@1048576 {} ::/Kernel/Driver/ \; ; \
	fi

__create_esp_iso:
	@ESP_MOUNT=$$(mktemp -d); \
	trap "sudo umount $$ESP_MOUNT 2>/dev/null; rmdir $$ESP_MOUNT 2>/dev/null" EXIT; \
	dd if=/dev/zero of=$(ESP_IMG) bs=1M count=40 2>/dev/null; \
	mkfs.fat -F32 $(ESP_IMG) 2>/dev/null; \
	sync; \
	sudo mount -o loop $(ESP_IMG) $$ESP_MOUNT || exit 1; \
	\
	sudo mkdir -p $$ESP_MOUNT/EFI/BOOT; \
	sudo mkdir -p $$ESP_MOUNT/BootManager; \
	sudo mkdir -p $$ESP_MOUNT/Kernel/Driver; \
	sudo mkdir -p $$ESP_MOUNT/Userland/SystemApps; \
	sudo mkdir -p $$ESP_MOUNT/Userland/UserApps; \
	\
	sudo cp $(BOOTX64_EFI) $$ESP_MOUNT/EFI/BOOT/BOOTX64.EFI; \
	sudo cp $(BOOTMANAGER_EFI) $$ESP_MOUNT/EFI/BOOT/BOOTMANAGER.EFI; \
	sudo cp $(KERNEL_ELF) $$ESP_MOUNT/Kernel/Kernel_Main.ELF; \
	sudo cp $(USERLAND_INIT_ELF) $$ESP_MOUNT/Userland/Userland.ELF; \
	\
	if [ -d $(DRIVER_STAGE_DIR) ]; then \
		find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -exec sudo cp {} $$ESP_MOUNT/Kernel/Driver/ \; ; \
	fi; \
	\
	if [ -d $(BOOT_RESOURCE_DIR) ]; then \
		sudo cp -r $(BOOT_RESOURCE_DIR) $$ESP_MOUNT/BootManager/; \
	fi; \
	\
	if [ -d $(BUILD_DIR)/Userland/SystemApps ]; then \
		find $(BUILD_DIR)/Userland/SystemApps -type f ! -name 'Userland.ELF' | while read f; do \
			rel_path=$${f#$(BUILD_DIR)/Userland/SystemApps}; \
			dest_file=$$ESP_MOUNT/Userland/SystemApps$${rel_path}; \
			sudo mkdir -p $$(dirname $$dest_file); \
			sudo cp $$f $$dest_file; \
		done; \
	fi; \
	\
	if [ -d $(BUILD_DIR)/Userland/UserApps ]; then \
		find $(BUILD_DIR)/Userland/UserApps -type f ! -name 'Userland.ELF' | while read f; do \
			rel_path=$${f#$(BUILD_DIR)/Userland/UserApps}; \
			dest_file=$$ESP_MOUNT/Userland/UserApps$${rel_path}; \
			sudo mkdir -p $$(dirname $$dest_file); \
			sudo cp $$f $$dest_file; \
		done; \
	fi; \
	\
	sync; \
	sudo umount $$ESP_MOUNT || exit 1

QEMU_COMMON = \
	-machine q35,smm=on \
	-smp 4,sockets=1,cores=4,threads=1 \
	-m 4G \
	-cpu Skylake-Server,+vmx,hv_vendor_id=null,-hypervisor \
	-device intel-iommu \
	-device qemu-xhci,id=xhci \
	-device usb-kbd,bus=xhci.0 \
	-device usb-mouse,bus=xhci.0 \
	-netdev user,id=net0 \
	-device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56 \
	-drive if=pflash,format=raw,readonly=on,file=${OVMF_CODE} \
	-drive if=pflash,format=raw,file=${OVMF_VARS} \
	-device ich9-ahci,id=sata \
	-drive file=${DISK_IMG},if=none,id=nvme0,format=raw \
	-device nvme,drive=nvme0,serial=deadbeef \
	-device ich9-intel-hda \
	-device hda-duplex \
	-rtc base=localtime,clock=host \
	-global ICH9-LPC.disable_s3=0 \
	-global ICH9-LPC.disable_s4=0 \
	-smbios type=1,manufacturer="Dell Inc.",product="XPS 8940" \
	-serial stdio

QEMU_IDE = \
	-drive format=raw,file=${IMAGE}

QEMU_USB = \
	-drive if=none,id=usbstick,format=raw,file=${IMAGE} \
	-device usb-storage,drive=usbstick

run_ide:
	@qemu-system-x86_64 $(QEMU_COMMON) $(QEMU_IDE)

run_usb:
	@qemu-system-x86_64 $(QEMU_COMMON) $(QEMU_USB)

run_bios:
	@qemu-system-x86_64 \
		-machine q35 \
		-smp 4,sockets=1,cores=4,threads=1 \
		-m 4G \
		-serial stdio \
		-drive format=raw,file=$(BIOS_IMAGE)
	  	
clean:
	@rm -rf $(BUILD_DIR) $(IMAGE_DIR)

-include $(USERLAND_INIT_OBJS:.o=.d)
-include $(USERLAND_APP_OBJS:.o=.d)
