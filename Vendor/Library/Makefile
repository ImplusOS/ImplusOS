ARCH ?= x86_64
REPO_ROOT := $(abspath ../..)
TOP_BUILD_DIR ?= $(REPO_ROOT)/Build/$(ARCH)
LIB_DIR := $(TOP_BUILD_DIR)/Vendor/Library
ZLIB_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/zlib
PNG_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/libpng
PNG_CONFIG_DIR := $(PNG_BUILD_DIR)/include
PNG_CONFIG_HEADER := $(PNG_CONFIG_DIR)/pnglibconf.h
SDL_DIR := SDL
SDL_PORT_DIR := $(REPO_ROOT)/Userland/Service/SDL_ImplusOS
SDL_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/SDL
SDL_CONFIG_DIR := $(SDL_BUILD_DIR)/include
LIB_Z := $(LIB_DIR)/libz.a
LIB_PNG := $(LIB_DIR)/libpng.a
LIB_SDL := $(LIB_DIR)/libSDL3.a

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-elf-
ARCH_CFLAGS := -mstrict-align -mno-outline-atomics
else ifeq ($(ARCH),x86_64)
CROSS_COMPILE ?= x86_64-elf-
ARCH_CFLAGS := -mcmodel=large -mno-red-zone
else
$(error Unsupported ARCH '$(ARCH)'. Use x86_64 or arm64.)
endif

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

ZLIB_SRCS := adler32.c compress.c crc32.c deflate.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c
ZLIB_OBJS := $(ZLIB_SRCS:%.c=$(ZLIB_BUILD_DIR)/%.o)

LIBPNG_SRCS := png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c pngsimd.c
LIBPNG_OBJS := $(LIBPNG_SRCS:%.c=$(PNG_BUILD_DIR)/%.o)

SDL_SRCS := \
	src/SDL.c \
	src/SDL_assert.c \
	src/SDL_error.c \
	src/SDL_guid.c \
	src/SDL_hashtable.c \
	src/SDL_hints.c \
	src/SDL_list.c \
	src/SDL_log.c \
	src/SDL_properties.c \
	src/SDL_utils.c \
	src/atomic/SDL_atomic.c \
	src/atomic/SDL_spinlock.c \
	src/audio/SDL_audio.c \
	src/audio/SDL_audiocvt.c \
	src/audio/SDL_audiodev.c \
	src/audio/SDL_audioqueue.c \
	src/audio/SDL_audioresample.c \
	src/audio/SDL_audiotypecvt.c \
	src/audio/SDL_mixer.c \
	src/audio/SDL_wave.c \
	src/core/SDL_core_unsupported.c \
	src/cpuinfo/SDL_cpuinfo.c \
	src/dialog/SDL_dialog.c \
	src/dialog/SDL_dialog_utils.c \
	src/dialog/dummy/SDL_dummydialog.c \
	src/events/SDL_categories.c \
	src/events/SDL_clipboardevents.c \
	src/events/SDL_displayevents.c \
	src/events/SDL_dropevents.c \
	src/events/SDL_events.c \
	src/events/SDL_eventwatch.c \
	src/events/SDL_keyboard.c \
	src/events/SDL_keymap.c \
	src/events/SDL_keysym_to_keycode.c \
	src/events/SDL_keysym_to_scancode.c \
	src/events/SDL_mouse.c \
	src/events/SDL_notificationevents.c \
	src/events/SDL_pen.c \
	src/events/SDL_quit.c \
	src/events/SDL_scancode_tables.c \
	src/events/SDL_touch.c \
	src/events/SDL_windowevents.c \
	src/events/imKStoUCS.c \
	src/filesystem/SDL_filesystem.c \
	src/filesystem/dummy/SDL_sysfilesystem.c \
	src/filesystem/dummy/SDL_sysfsops.c \
	src/io/SDL_asyncio.c \
	src/io/SDL_iostream.c \
	src/io/generic/SDL_asyncio_generic.c \
	src/loadso/dummy/SDL_sysloadso.c \
	src/locale/SDL_locale.c \
	src/locale/dummy/SDL_syslocale.c \
	src/main/SDL_main_callbacks.c \
	src/main/SDL_runapp.c \
	src/main/generic/SDL_sysmain_callbacks.c \
	src/misc/SDL_url.c \
	src/misc/dummy/SDL_sysurl.c \
	src/notification/SDL_notification.c \
	src/notification/dummy/SDL_dummynotification.c \
	src/process/SDL_process.c \
	src/process/dummy/SDL_dummyprocess.c \
	src/render/SDL_render.c \
	src/render/SDL_render_unsupported.c \
	src/render/SDL_yuv_sw.c \
	src/render/software/SDL_blendfillrect.c \
	src/render/software/SDL_blendline.c \
	src/render/software/SDL_blendpoint.c \
	src/render/software/SDL_drawline.c \
	src/render/software/SDL_drawpoint.c \
	src/render/software/SDL_render_sw.c \
	src/render/software/SDL_triangle.c \
	src/stdlib/SDL_crc16.c \
	src/stdlib/SDL_crc32.c \
	src/stdlib/SDL_getenv.c \
	src/stdlib/SDL_iconv.c \
	src/stdlib/SDL_malloc.c \
	src/stdlib/SDL_memcpy.c \
	src/stdlib/SDL_memmove.c \
	src/stdlib/SDL_memset.c \
	src/stdlib/SDL_mslibc.c \
	src/stdlib/SDL_murmur3.c \
	src/stdlib/SDL_qsort.c \
	src/stdlib/SDL_random.c \
	src/stdlib/SDL_stdlib.c \
	src/stdlib/SDL_string.c \
	src/stdlib/SDL_strtokr.c \
	src/storage/SDL_storage.c \
	src/storage/generic/SDL_genericstorage.c \
	src/thread/SDL_thread.c \
	src/thread/generic/SDL_syscond.c \
	src/thread/generic/SDL_sysmutex.c \
	src/thread/generic/SDL_sysrwlock.c \
	src/thread/generic/SDL_syssem.c \
	src/thread/generic/SDL_systls.c \
	src/time/SDL_time.c \
	src/timer/SDL_timer.c \
	src/tray/SDL_tray_utils.c \
	src/tray/dummy/SDL_tray.c \
	src/video/SDL_RLEaccel.c \
	src/video/SDL_blit.c \
	src/video/SDL_blit_0.c \
	src/video/SDL_blit_1.c \
	src/video/SDL_blit_A.c \
	src/video/SDL_blit_N.c \
	src/video/SDL_blit_auto.c \
	src/video/SDL_blit_copy.c \
	src/video/SDL_blit_slow.c \
	src/video/SDL_bmp.c \
	src/video/SDL_clipboard.c \
	src/video/SDL_fillrect.c \
	src/video/SDL_pixels.c \
	src/video/SDL_rect.c \
	src/video/SDL_rotate.c \
	src/video/SDL_stb.c \
	src/video/SDL_stretch.c \
	src/video/SDL_surface.c \
	src/video/SDL_video.c \
	src/video/SDL_video_unsupported.c \
	src/video/SDL_yuv.c

SDL_LIBM_SRCS := $(patsubst $(SDL_DIR)/%,%,$(shell find $(SDL_DIR)/src/libm -name '*.c' 2>/dev/null | sort))
SDL_PORT_SRCS := SDL_implus_audio.c SDL_implus_gamepad.c SDL_implus_thread.c SDL_implus_timer.c SDL_implus_video.c
SDL_OBJS := \
	$(SDL_SRCS:%.c=$(SDL_BUILD_DIR)/%.o) \
	$(SDL_LIBM_SRCS:%.c=$(SDL_BUILD_DIR)/%.o) \
	$(SDL_PORT_SRCS:%.c=$(SDL_BUILD_DIR)/port/%.o)

COMMON_CFLAGS := -ffreestanding -fno-pic -fno-builtin $(ARCH_CFLAGS) -nostdlib -Os -g0 -ffunction-sections -fdata-sections -I$(REPO_ROOT)/libc/I_libc/include -Wno-error -include stddef.h
ZLIB_CFLAGS := $(COMMON_CFLAGS) -I. -Izlib -DNO_GZFILE_OPS
LIBPNG_CFLAGS := $(COMMON_CFLAGS) -I$(PNG_CONFIG_DIR) -Ilibpng -Izlib
SDL_CFLAGS := $(COMMON_CFLAGS) \
	-DSDL_PLATFORM_PRIVATE=1 \
	-DSDL_STATIC_LIB=1 \
	-I$(SDL_CONFIG_DIR) \
	-I$(SDL_PORT_DIR) \
	-I$(SDL_DIR)/include \
	-I$(SDL_DIR)/include/build_config \
	-I$(SDL_DIR)/src \
	-I$(REPO_ROOT) \
	-Wno-unused-parameter \
	-Wno-unused-function \
	-Wno-missing-field-initializers

all: $(LIB_Z) $(LIB_PNG) $(LIB_SDL)

$(LIB_Z): $(ZLIB_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(ZLIB_BUILD_DIR)/%.o: zlib/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ZLIB_CFLAGS) -c $< -o $@

$(LIB_PNG): $(LIBPNG_OBJS) $(LIB_Z)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $(LIBPNG_OBJS)

$(PNG_CONFIG_HEADER): libpng/pnglibconf.h.prebuilt
	@mkdir -p $(dir $@)
	sed '/^#define PNG_TARGET_SPECIFIC_CODE_SUPPORTED/d' $< > $@

$(PNG_BUILD_DIR)/%.o: libpng/%.c $(PNG_CONFIG_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(LIBPNG_CFLAGS) -c $< -o $@

$(SDL_CONFIG_DIR)/SDL_build_config.h: $(SDL_PORT_DIR)/SDL_build_config.h $(SDL_PORT_DIR)/SDL_build_config_private.h $(SDL_PORT_DIR)/SDL_end_config_private.h
	@mkdir -p $(dir $@)
	cp $(SDL_PORT_DIR)/SDL_build_config.h $(SDL_CONFIG_DIR)/SDL_build_config.h
	cp $(SDL_PORT_DIR)/SDL_build_config_private.h $(SDL_CONFIG_DIR)/SDL_build_config_private.h
	cp $(SDL_PORT_DIR)/SDL_end_config_private.h $(SDL_CONFIG_DIR)/SDL_end_config_private.h

$(SDL_BUILD_DIR)/%.o: $(SDL_DIR)/%.c $(SDL_CONFIG_DIR)/SDL_build_config.h
	@mkdir -p $(dir $@)
	$(CC) $(SDL_CFLAGS) -c $< -o $@

$(SDL_BUILD_DIR)/port/%.o: $(SDL_PORT_DIR)/%.c $(SDL_CONFIG_DIR)/SDL_build_config.h
	@mkdir -p $(dir $@)
	$(CC) $(SDL_CFLAGS) -c $< -o $@

$(LIB_SDL): $(SDL_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

clean:
	rm -rf $(ZLIB_BUILD_DIR) $(PNG_BUILD_DIR) $(SDL_BUILD_DIR) $(LIB_Z) $(LIB_PNG) $(LIB_SDL)
