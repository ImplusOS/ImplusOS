CC  := x86_64-elf-gcc
LD  := x86_64-elf-ld
AR  := x86_64-elf-ar

APP_NAME  := gtk_wm
SRC_DIR   := .
BUILD_DIR := ../../../../Build/Userland/UserApps/$(APP_NAME)
ELF       := $(BUILD_DIR)/$(APP_NAME).ELF

GTKPORT   := ../../../GTKPort/sysroot

APP_SRC  := $(SRC_DIR)/main.c
APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)

include ../../../AppCommon.mk

OBJS := $(APP_OBJS) $(COMMON_OBJS)

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -mcmodel=large -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
          -I../../../../libc/include -I../../../../libc/include/sys \
          -I../../../../Userland \
          -I../../../../Userland/API \
          -I$(GTKPORT)/include \
          -I$(GTKPORT)/include/cairo \
          -I$(GTKPORT)/include/glib-2.0 \
          -I$(GTKPORT)/include/pango-1.0 \
          -Wall -Wextra -O2 -MMD -MP -w

LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none

GTK_LIBS := \
    $(GTKPORT)/lib/libcairo.a \
    $(GTKPORT)/lib/libpango-1.0.a \
    $(GTKPORT)/lib/libglib-2.0.a \
    $(GTKPORT)/lib/libfreetype.a \
    $(GTKPORT)/lib/libfontconfig.a \
    $(GTKPORT)/lib/libharfbuzz.a \
    $(GTKPORT)/lib/libfribidi.a \
    $(GTKPORT)/lib/libgdk_pixbuf-2.0.a \
    $(GTKPORT)/lib/libpng.a \
    $(GTKPORT)/lib/libz.a

.PHONY: all clean gtkport

all: gtkport $(ELF)

gtkport:
	@$(MAKE) -C ../../../GTKPort

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(GTK_LIBS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) --start-group $(GTK_LIBS) --end-group -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d)
