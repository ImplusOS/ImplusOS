ROOT_DIR ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

CC ?= x86_64-elf-gcc
LD ?= x86_64-elf-ld

DRIVER_MODULE_CFLAGS ?= \
	-I$(ROOT_DIR)/Kernel -I$(ROOT_DIR)/Thirdparty -I$(ROOT_DIR)/ThirdParty -I$(ROOT_DIR)/libc/include \
	-ffreestanding -fno-stack-protector -fPIC -fno-builtin \
	-mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
	-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
	-MMD -MP \
	-DIMPLUS_DRIVER_MODULE -DKERNEL
DRIVER_MODULE_LDFLAGS ?= -nostdlib -shared --build-id=none -Bsymbolic -e driver_module_init -z max-page-size=4096

DRIVER_SRCS ?= $(sort $(shell find . -type f -name '*.c' -print | sed 's|^\./||'))
DRIVER_BUILD_DIR ?= $(ROOT_DIR)/Build/Modules/$(DRIVER_NAME)
DRIVER_ELF ?= $(DRIVER_BUILD_DIR)/$(DRIVER_NAME).ELF
DRIVER_OBJS := $(patsubst %.c,$(DRIVER_BUILD_DIR)/%.o,$(DRIVER_SRCS))

.PHONY: all clean

all: $(DRIVER_ELF)

$(DRIVER_BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(DRIVER_MODULE_CFLAGS) -c $< -o $@

$(DRIVER_ELF): $(DRIVER_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(DRIVER_MODULE_LDFLAGS) $^ -o $@

clean:
	rm -rf $(DRIVER_BUILD_DIR)

-include $(DRIVER_OBJS:.o=.d)
