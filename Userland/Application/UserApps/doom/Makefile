.PHONY: all clean copy_resources

APP_NAME := doom
SRC_DIR  := .
DOOM_DIR := doomgeneric/doomgeneric
TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
BUILD_DIR := $(TOP_BUILD_DIR)/Userland/UserApps/$(APP_NAME)
ELF      := $(BUILD_DIR)/$(APP_NAME).ELF

APP_SRC := main.c doom_implusos.c doom_audio_implusos.c

DOOM_SRC := \
	$(DOOM_DIR)/dummy.c \
	$(DOOM_DIR)/am_map.c \
	$(DOOM_DIR)/doomdef.c \
	$(DOOM_DIR)/doomstat.c \
	$(DOOM_DIR)/dstrings.c \
	$(DOOM_DIR)/d_event.c \
	$(DOOM_DIR)/d_items.c \
	$(DOOM_DIR)/d_iwad.c \
	$(DOOM_DIR)/d_loop.c \
	$(DOOM_DIR)/d_main.c \
	$(DOOM_DIR)/d_mode.c \
	$(DOOM_DIR)/d_net.c \
	$(DOOM_DIR)/f_finale.c \
	$(DOOM_DIR)/f_wipe.c \
	$(DOOM_DIR)/g_game.c \
	$(DOOM_DIR)/hu_lib.c \
	$(DOOM_DIR)/hu_stuff.c \
	$(DOOM_DIR)/info.c \
	$(DOOM_DIR)/i_cdmus.c \
	$(DOOM_DIR)/i_endoom.c \
	$(DOOM_DIR)/i_joystick.c \
	$(DOOM_DIR)/i_scale.c \
	$(DOOM_DIR)/i_sound.c \
	$(DOOM_DIR)/i_system.c \
	$(DOOM_DIR)/i_timer.c \
	$(DOOM_DIR)/memio.c \
	$(DOOM_DIR)/m_argv.c \
	$(DOOM_DIR)/m_bbox.c \
	$(DOOM_DIR)/m_cheat.c \
	$(DOOM_DIR)/m_config.c \
	$(DOOM_DIR)/m_controls.c \
	$(DOOM_DIR)/m_fixed.c \
	$(DOOM_DIR)/m_menu.c \
	$(DOOM_DIR)/m_misc.c \
	$(DOOM_DIR)/m_random.c \
	$(DOOM_DIR)/p_ceilng.c \
	$(DOOM_DIR)/p_doors.c \
	$(DOOM_DIR)/p_enemy.c \
	$(DOOM_DIR)/p_floor.c \
	$(DOOM_DIR)/p_inter.c \
	$(DOOM_DIR)/p_lights.c \
	$(DOOM_DIR)/p_map.c \
	$(DOOM_DIR)/p_maputl.c \
	$(DOOM_DIR)/p_mobj.c \
	$(DOOM_DIR)/p_plats.c \
	$(DOOM_DIR)/p_pspr.c \
	$(DOOM_DIR)/p_saveg.c \
	$(DOOM_DIR)/p_setup.c \
	$(DOOM_DIR)/p_sight.c \
	$(DOOM_DIR)/p_spec.c \
	$(DOOM_DIR)/p_switch.c \
	$(DOOM_DIR)/p_telept.c \
	$(DOOM_DIR)/p_tick.c \
	$(DOOM_DIR)/p_user.c \
	$(DOOM_DIR)/r_bsp.c \
	$(DOOM_DIR)/r_data.c \
	$(DOOM_DIR)/r_draw.c \
	$(DOOM_DIR)/r_main.c \
	$(DOOM_DIR)/r_plane.c \
	$(DOOM_DIR)/r_segs.c \
	$(DOOM_DIR)/r_sky.c \
	$(DOOM_DIR)/r_things.c \
	$(DOOM_DIR)/sha1.c \
	$(DOOM_DIR)/sounds.c \
	$(DOOM_DIR)/statdump.c \
	$(DOOM_DIR)/st_lib.c \
	$(DOOM_DIR)/st_stuff.c \
	$(DOOM_DIR)/s_sound.c \
	$(DOOM_DIR)/tables.c \
	$(DOOM_DIR)/v_video.c \
	$(DOOM_DIR)/wi_stuff.c \
	$(DOOM_DIR)/w_checksum.c \
	$(DOOM_DIR)/w_file.c \
	$(DOOM_DIR)/w_file_stdc.c \
	$(DOOM_DIR)/w_main.c \
	$(DOOM_DIR)/w_wad.c \
	$(DOOM_DIR)/z_zone.c \
	$(DOOM_DIR)/i_input.c \
	$(DOOM_DIR)/i_video.c \
	$(DOOM_DIR)/doomgeneric.c

APP_OBJS := $(APP_SRC:%.c=$(BUILD_DIR)/%.o)
DOOM_OBJS := $(DOOM_SRC:%.c=$(BUILD_DIR)/%.o)

include ../../../AppCommon.mk

LIBSDL := $(TOP_BUILD_DIR)/Vendor/Library/libSDL3.a
RESOURCE_DIR := Resource
OBJS := $(APP_OBJS) $(DOOM_OBJS) $(COMMON_OBJS)

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -DSDL_MAIN_HANDLED -DSDL_PLATFORM_PRIVATE=1 -DSDL_STATIC_LIB=1 \
          -DFEATURE_SOUND=1 \
          $(ARCH_CFLAGS) -nostdlib -nostartfiles -nodefaultlibs \
          -Icompat \
          -I$(DOOM_DIR) \
          -I../../../../libc/I_libc/include -I../../../../libc/I_libc/include/sys \
          -I../../../../Userland \
          -I../../../../Userland/API \
          -I../../../../Vendor/Library/SDL/include \
          -Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow -MMD -MP
LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none -L$(TOP_BUILD_DIR)/Vendor/Library

all: $(ELF) copy_resources

copy_resources:
	@if [ -d $(RESOURCE_DIR) ]; then \
		mkdir -p $(BUILD_DIR); \
		cp -a $(RESOURCE_DIR) $(BUILD_DIR)/; \
	fi

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) $(LIBSDL)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $(OBJS) $(LIBSDL) -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(APP_OBJS:.o=.d) $(DOOM_OBJS:.o=.d) $(COMMON_DEPS)
