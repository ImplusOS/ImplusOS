ARCH ?= x86_64

ifeq ($(ARCH),x86_64)
CC := x86_64-elf-gcc
LD := x86_64-elf-ld
NASM := nasm
ARCH_DIR := Arch/x86_64
ARCH_CFLAGS := -mcmodel=large -mno-red-zone -DPLATFORM_X86_64
ARCH_ASM_FORMAT := elf64
KERNEL_LDSCRIPT := Arch/x86_64/linker/linker.ld
else ifeq ($(ARCH),arm64)
CC := aarch64-elf-gcc
LD := aarch64-elf-ld
NASM := false
ARCH_DIR := Arch/arm64
ARCH_CFLAGS := -mstrict-align -mno-outline-atomics -DPLATFORM_ARM64
KERNEL_LDSCRIPT := Arch/arm64/linker/linker.ld
else
$(error Unsupported ARCH: $(ARCH))
endif

KERNEL_CFLAGS := \
	-I$(KERNEL_ROOT) \
	-I$(KERNEL_ROOT)/include \
	-I$(KERNEL_ROOT)/Arch/$(ARCH) \
	-I$(KERNEL_ROOT)/Core \
	-I$(KERNEL_ROOT)/Platform \
	-I$(ROOT_DIR)/Thirdparty \
	-I$(ROOT_DIR)/ThirdParty \
	-I$(ROOT_DIR)/libc/I_libc/include \
	-I$(ROOT_DIR)/ShareLib \
	-ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-PIE -fno-plt -fno-builtin \
	-nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
	-MMD -MP -DKERNEL \
	$(ARCH_CFLAGS)

KERNEL_LDFLAGS := -nostdlib --build-id=none -e kernel_main -T $(KERNEL_LDSCRIPT)
