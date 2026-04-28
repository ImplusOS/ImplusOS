CC := x86_64-elf-gcc
LD := x86_64-elf-ld

APP_NAME := com_ImplusOS_musl_test
BUILD_DIR := ../../../../Build/Userland/UserApps/$(APP_NAME)
ELF := $(BUILD_DIR)/$(APP_NAME).ELF
OBJ := $(BUILD_DIR)/main.o

MUSL_DIR := ../../../../musl
MUSL_LIB := $(MUSL_DIR)/lib/libc.a

INCLUDES := \
	-I$(MUSL_DIR)/include \
	-I$(MUSL_DIR)/arch/implus \
	-I$(MUSL_DIR)/arch/generic \
	-I$(MUSL_DIR)/obj/include

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -mcmodel=large -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
          -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow \
          -std=c11 -Os $(INCLUDES)
LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none

all: check_musl $(ELF)

check_musl:
	@test -f $(MUSL_LIB) || (echo "Missing $(MUSL_LIB). Build musl first: make -f Makefile.musl musl_lib" && false)

$(OBJ): main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJ) $(MUSL_LIB)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $< $(MUSL_LIB) -o $@

clean:
	rm -rf $(BUILD_DIR)
