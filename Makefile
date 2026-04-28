.PHONY: all run_usb run_ide clean image app_build driver_build driver_stage gtkport_build

ARCH := x86_64
CC   = x86_64-elf-gcc
CXX  = x86_64-elf-g++
LD   = x86_64-elf-ld
NASM = nasm

BUILD_DIR := Build
IMAGE_DIR := Image
IMAGE     := $(IMAGE_DIR)/ImplusOS.iso

OVMF_CODE := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS := OVMF_VARS_4M.fd
DISK_IMG  := disk.qcow2

KERNEL_DIR   := Kernel
USERLAND_DIR := Userland

KERNEL_ELF        := $(BUILD_DIR)/Kernel/Kernel_Main.ELF
BOOTX64_EFI       := $(BUILD_DIR)/Loader/BOOTX64.EFI
USERLAND_INIT_ELF := $(BUILD_DIR)/Userland/Userland.ELF

KERNEL_CFLAGS := \
	-IKernel -IThirdParty -Ilibc/include \
	-ffreestanding -fno-stack-protector -fPIE -fno-plt -fno-builtin -mcmodel=large \
	-mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
	-MMD -MP -DKERNEL

KERNEL_LDFLAGS := -shared -Bsymbolic -nostdlib --build-id=none -e kernel_main -T Kernel/Kernel_Main.ld

LOADER_CFLAGS := \
	-I/usr/include/efi \
	-I/usr/include/efi/x86_64 \
	-I/usr/include/efi/protocol \
	-I../libc/include \
	-ffreestanding -fpic -fshort-wchar -fno-stack-protector \
	-fno-builtin -mno-red-zone \
	-Wall -Wextra -DEFI_FUNCTION_WRAPPER

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

DRIVER_MAKEFILES := $(shell find Kernel/Drivers/DrvMain/Server -name Makefile -print 2>/dev/null | sort)
DRIVER_DIRS := $(sort $(patsubst %/,%,$(dir $(DRIVER_MAKEFILES))))
DRIVER_BUILD_ROOT := $(BUILD_DIR)/Modules
DRIVER_STAGE_DIR := $(BUILD_DIR)/Kernel/Drivers

KERNEL_C_SRCS := \
	libc/src/assert.c \
	libc/src/math.c \
	libc/src/stdlib.c \
	libc/src/string.c \
	libc/src/stdio.c \
	libc/src/errno.c \
	Kernel/Kernel_Main.c \
	Kernel/Timer/Timer.c \
	Kernel/Boot/LoadBar.c \
	Kernel/Platform/ACPI/ACPI.c \
	Kernel/Platform/APIC/LAPIC.c \
	Kernel/Platform/APIC/IOAPIC.c \
	Kernel/Platform/Interrupts/Interrupts.c \
	Kernel/Memory/Memory_Main.c \
	Kernel/Memory/DMA_Memory.c \
	Kernel/Paging/Paging_Main.c \
	Kernel/SMP/SMP_Main.c \
	Kernel/IDT/IDT_Main.c \
	Kernel/IO/IO_Main.c \
	Kernel/IO/Protocol/ATA/Protocol_ATA.c \
	Kernel/IO/Protocol/USB_MassStorage/Protocol_USB_MassStorage.c \
	Kernel/Drivers/DrvMain/Client/USB/USB_Client.c \
	Kernel/Drivers/DrvMain/Client/FileSystem/FAT32/FAT32_Client.c \
	Kernel/Drivers/DrvMain/Client/Display/Display_Main.c \
	Kernel/Drivers/DrvMain/Client/NIC/NIC.c \
	Kernel/Drivers/DrvMain/Client/PS2/PS2_Client.c \
	Kernel/Drivers/DrvMain/Client/PCI/PCI_Client.c \
	Kernel/Drivers/DrvMain/Server/Display/ImplusOS_Generic/ImplusOS_Generic.c \
	Kernel/Drivers/DrvMain/Server/NIC/VirtIONet/VirtIONet.c \
	Kernel/GDT/GDT_Main.c \
	Kernel/ELF/ELF_Loader.c \
	Kernel/Drivers/Module/DriverModule.c \
	Kernel/Drivers/Module/DriverManager.c \
	Kernel/Drivers/Module/DriverFrameworkAPI.c \
	Kernel/Drivers/Module/DriverSelect.c \
	Kernel/Ethernet/Ethernet.c \
	Kernel/ARP/ARP.c \
	Kernel/Network/IPv4.c \
	Kernel/Network/Network_Main.c \
	Kernel/Network/UDP/UDP.c \
	Kernel/Network/TCP/TCP.c \
	Kernel/Network/ICMP/ICMP.c \
	Kernel/Network/DHCP/DHCP.c \
	Kernel/VFS/VFS.c \
	Kernel/ProcessManager/ProcessManager_Create.c \
	Kernel/Syscall/Syscall_Init.c \
	Kernel/Syscall/Syscall_File.c \
	Kernel/Syscall/Syscall_Dispatch.c \
	Kernel/IPC/IPC_Main.c \
	Kernel/WindowManager/WindowManager_Kernel.c \
	Kernel/Drivers/RTC/RTC.c \
	Kernel/Debbuger/Serial/Serial.c \
	Kernel/Debbuger/printf/printf.c \
	Kernel/Debbuger/Panic/Panic.c \
	Kernel/Syscall/Syscall_VM.c \
	Kernel/Syscall/Syscall_Epoll.c \
	Kernel/Syscall/Syscall_Futex.c \
	Kernel/Syscall/Syscall_Signal_Musl.c \
	Kernel/Syscall/Syscall_Misc_Musl.c \
	Kernel/Syscall/Syscall_Clock.c \
	Kernel/Drivers/DrvMain/Client/DRM/DRM_Client.c \
	Kernel/Drivers/DrvMain/Client/Evdev/Evdev_Client.c \
	Kernel/Drivers/DrvMain/Client/UnixSocket/UnixSocket.c \
	Kernel/Platform/VTx/VTx.c \
	Kernel/Platform/VTx/EPT.c \
	Kernel/Platform/VTx/VMExit.c \
	Kernel/Drivers/DrvMain/Client/KVM/KVM_Client.c

KERNEL_ASM_SRCS := \
	Kernel/Paging/Paging.asm \
	Kernel/GDT/GDT.asm \
	Kernel/IDT/IDT.asm \
	Kernel/Syscall/Syscall_Entry.asm \
	Kernel/SMP/SMP_Trampoline.asm \
	Kernel/Platform/VTx/VMEntry.asm

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
	Userland/Application/SystemApps/com_ImplusOS_testApp/UserApp.c \
	Userland/Application/SystemApps/com_ImplusOS_testApp/PNG_Decoder/PNG_Decoder.c \
	Userland/Application/SystemApps/com_ImplusOS_ipc_test/UserApp.c \
	Userland/Syscalls.c \
	Userland/API/XMLParser.c \
	Userland/Application/UserApps/com_ImplusOS_exampleApp/exampleApp.c \
	Userland/Application/UserApps/com_ImplusOS_NetworkTest/networkTest.c \
	Userland/Application/UserApps/com_ImplusOS_wayland_demo/wayland_demo.c \
	Userland/NetworkStack/DNS/DNS.c

KERNEL_OBJS       := $(patsubst Kernel/%.c,$(BUILD_DIR)/Kernel/%.o,$(filter Kernel/%.c,$(KERNEL_C_SRCS))) \
                     $(patsubst libc/%.c,$(BUILD_DIR)/Kernel/libc/%.o,$(filter libc/%.c,$(KERNEL_C_SRCS))) \
                     $(KERNEL_ASM_SRCS:%.asm=$(BUILD_DIR)/%.o)
USERLAND_INIT_OBJS := $(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_C_SRCS))) \
                      $(patsubst libc/%.c,$(BUILD_DIR)/Userland/libc/%.o,$(filter libc/%.c,$(USERLAND_C_SRCS)))
USERLAND_APP_OBJS  := $(patsubst Userland/%.c,$(BUILD_DIR)/Userland/%.o,$(filter Userland/%.c,$(USERLAND_APP_C_SRCS))) \
                      $(patsubst libc/%.c,$(BUILD_DIR)/Userland/libc/%.o,$(filter libc/%.c,$(USERLAND_APP_C_SRCS)))

all: $(BOOTX64_EFI) \
     $(KERNEL_ELF) \
     $(USERLAND_INIT_ELF) \
     driver_stage \
     gtkport_build \
     app_build

gtkport_build:
	@$(MAKE) -C Userland/GTKPort

app_build: $(USERLAND_INIT_OBJS) gtkport_build
	@$(MAKE) -C Userland/Application/SystemApps/com_ImplusOS_system
	@$(MAKE) -C Userland/Application/SystemApps/com_ImplusOS_windowmanager
	@$(MAKE) -C Userland/Application/SystemApps/com_ImplusOS_mousemanager
	@$(MAKE) -C Userland/Application/SystemApps/com_ImplusOS_shell
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_exampleApp
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_NetworkTest
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_editor
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_filemanager
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_clock
	@$(MAKE) -C Userland/Application/UserApps/org_ffmpeg_git_ffmpeg_git
	@$(MAKE) -C Userland/Application/UserApps/tinywl
	@$(MAKE) -C Userland/Application/UserApps/gtk_wm
	@$(MAKE) -C Userland/Application/UserApps/netsurf
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

$(BUILD_DIR)/Loader/Loader.o: BootLoader/Loader.c
	mkdir -p $(dir $@)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@

$(BOOTX64_EFI): $(BUILD_DIR)/Loader/Loader.o
	mkdir -p $(dir $@)
	$(LD) -nostdlib -znocombreloc \
		-T /usr/lib/elf_x86_64_efi.lds \
		-shared -Bsymbolic \
		/usr/lib/crt0-efi-x86_64.o \
		$< \
		/usr/lib/libefi.a \
		/usr/lib/libgnuefi.a \
		-o $@.so
	objcopy -j .text -j .sdata -j .data -j .dynamic \
		-j .dynsym -j .rel -j .rela -j .reloc -j .rodata -j .rdata -j .rodata.* \
		--target=efi-app-x86_64 $@.so $@
	rm -f $@.so

$(BUILD_DIR)/Kernel/%.o: Kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Kernel/%.o: Kernel/%.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS)
	mkdir -p $(dir $@)
	$(LD) $(KERNEL_LDFLAGS) $^ -o $@

$(BUILD_DIR)/Userland/%.o: Userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/libc/%.o: libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Kernel/libc/%.o: libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD_DIR)/Userland/%.o: Userland/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(USERLAND_CXXFLAGS) -c $< -o $@

$(USERLAND_INIT_ELF): $(USERLAND_INIT_OBJS)
	mkdir -p $(dir $@)
	$(LD) $(USERLAND_LDFLAGS) $^ -o $@

USERLAND_ELFS := $(shell find $(BUILD_DIR)/Userland -name '*.ELF' 2>/dev/null)
BOOT_RESOURCE_DIR := BootLoader/Resource

__mount_image:
	@MOUNT_POINT=$$(mktemp -d); \
	LOOP_DEVICE=$$(sudo losetup -Pf --show $(IMAGE)) || exit 1; \
	trap "sudo losetup -d $$LOOP_DEVICE 2>/dev/null; rmdir $$MOUNT_POINT 2>/dev/null" EXIT; \
	sudo mkfs.fat -F32 $${LOOP_DEVICE}p1 || exit 1; \
	sudo mount $${LOOP_DEVICE}p1 $$MOUNT_POINT || exit 1; \
	sudo mkdir -p $$MOUNT_POINT/EFI/BOOT $$MOUNT_POINT/Kernel/Driver $$MOUNT_POINT/Userland; \
	sudo cp $(BOOTX64_EFI) $$MOUNT_POINT/EFI/BOOT/BOOTX64.EFI; \
	sudo cp $(KERNEL_ELF) $$MOUNT_POINT/Kernel/Kernel_Main.ELF; \
	sudo cp $(USERLAND_INIT_ELF) $$MOUNT_POINT/Userland/Userland.ELF; \
	if [ -d $(DRIVER_STAGE_DIR) ]; then \
		find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -exec sudo cp {} $$MOUNT_POINT/Kernel/Driver/ \; ; \
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
	@rsync -a $(BOOT_RESOURCE_DIR) $(ISO_ROOT)/EFI/BOOT/
	@xorriso -as mkisofs -R -J -V "ImplusOS Clesk 0.2-beta" \
		-o $(IMAGE) \
		-eltorito-alt-boot -e esp.iso -no-emul-boot \
		$(ISO_ROOT)

	@cp $(IMAGE) Qemu/Test/Resource/ImplusOS.iso

__create_esp_iso:
	@ESP_MOUNT=$$(mktemp -d); \
	trap "sudo umount $$ESP_MOUNT 2>/dev/null; rmdir $$ESP_MOUNT 2>/dev/null" EXIT; \
	dd if=/dev/zero of=$(ESP_IMG) bs=1M count=180 2>/dev/null; \
	mkfs.fat -F32 $(ESP_IMG) 2>/dev/null; \
	sync; \
	sudo mount -o loop $(ESP_IMG) $$ESP_MOUNT || exit 1; \
	\
	sudo mkdir -p $$ESP_MOUNT/EFI/BOOT; \
	sudo mkdir -p $$ESP_MOUNT/Kernel/Driver; \
	sudo mkdir -p $$ESP_MOUNT/Userland/SystemApps; \
	sudo mkdir -p $$ESP_MOUNT/Userland/UserApps; \
	\
	sudo cp $(BOOTX64_EFI) $$ESP_MOUNT/EFI/BOOT/BOOTX64.EFI; \
	sudo cp $(KERNEL_ELF) $$ESP_MOUNT/Kernel/Kernel_Main.ELF; \
	sudo cp $(USERLAND_INIT_ELF) $$ESP_MOUNT/Userland/Userland.ELF; \
	\
	if [ -d $(DRIVER_STAGE_DIR) ]; then \
		find $(DRIVER_STAGE_DIR) -maxdepth 1 -type f -name '*.ELF' -exec sudo cp {} $$ESP_MOUNT/Kernel/Driver/ \; ; \
	fi; \
	\
	if [ -d $(BOOT_RESOURCE_DIR) ]; then \
		sudo cp -r $(BOOT_RESOURCE_DIR) $$ESP_MOUNT/EFI/BOOT/Resource/; \
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
	if [ -f /usr/share/OVMF/OVMF_CODE_4M.fd ]; then \
		sudo mkdir -p $$ESP_MOUNT/Userland/UserApps/com_ImplusOS_vm; \
		sudo cp /usr/share/OVMF/OVMF_CODE_4M.fd $$ESP_MOUNT/Userland/UserApps/com_ImplusOS_vm/OVMF.fd; \
	fi; \
	\
	sync; \
	sudo umount $$ESP_MOUNT || exit 1

QEMU_COMMON = \
	-enable-kvm \
	-machine q35,accel=kvm,smm=on \
	-smp 4,sockets=1,cores=4,threads=1 \
	-m 4G \
	-cpu host,kvm=on,+vmx,hv_vendor_id=null,-hypervisor \
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
	  	
clean:
	@rm -rf $(BUILD_DIR) $(IMAGE_DIR)

-include $(KERNEL_OBJS:.o=.d)
-include $(USERLAND_INIT_OBJS:.o=.d)
-include $(USERLAND_APP_OBJS:.o=.d)
