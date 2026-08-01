.PHONY: all clean copy_resources

APP_NAME := com_ImplusOS_dltest
SRC_DIR  := .
TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := $(shell find $(SRC_DIR) -name '*.c' -not -name 'libdl*.c')
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)

include ../../../AppCommon.mk

OBJS := $(APP_OBJS) $(COMMON_OBJS)

CFLAGS := -Os -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -ffunction-sections -fdata-sections \
          $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -I../../../../libc/I_libc/include -I../../../../libc/I_libc/include/sys \
		  -I../../../../Userland \
		  -I../../../../Userland/API \
		  -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP

LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none --gc-sections

SO_CFLAGS := -Os -ffreestanding -fno-stack-protector -fPIC -fno-builtin \
             $(ARCH_CFLAGS) -nostdlib -nodefaultlibs \
             -I../../../../libc/I_libc/include -I../../../../libc/I_libc/include/sys \
             -Wall -Wextra -Wtype-limits

LIBS := $(BUILD_DIR)/libdlbase.so $(BUILD_DIR)/libdlmath.so $(BUILD_DIR)/libdlplain.so

all: $(ELF) $(LIBS)

$(BUILD_DIR)/libdlbase.so: libdlbase.c
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -c libdlbase.c -o $(BUILD_DIR)/libdlbase.o
	$(LD) -shared -nostdlib --build-id=none -soname libdlbase.so -o $@ $(BUILD_DIR)/libdlbase.o

$(BUILD_DIR)/libdlmath.so: libdlmath.c $(BUILD_DIR)/libdlbase.so
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -ftls-model=initial-exec -c libdlmath.c -o $(BUILD_DIR)/libdlmath.o
	$(LD) -shared -nostdlib --build-id=none -soname libdlmath.so -o $@ $(BUILD_DIR)/libdlmath.o $(BUILD_DIR)/libdlbase.so

$(BUILD_DIR)/libdlplain.so: libdlplain.c
	@mkdir -p $(dir $@)
	$(CC) $(SO_CFLAGS) -c libdlplain.c -o $(BUILD_DIR)/libdlplain.o
	$(LD) -shared -nostdlib --build-id=none -soname libdlplain.so -o $@ $(BUILD_DIR)/libdlplain.o

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d) $(COMMON_DEPS)
