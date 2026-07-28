ARCH ?= x86_64
REPO_ROOT := $(abspath ../..)
TOP_BUILD_DIR ?= $(REPO_ROOT)/Build/$(ARCH)
LIB_DIR := $(TOP_BUILD_DIR)/Vendor/Library
ZLIB_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/zlib
PNG_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/libpng
PNG_CONFIG_DIR := $(PNG_BUILD_DIR)/include
PNG_CONFIG_HEADER := $(PNG_CONFIG_DIR)/pnglibconf.h
JPEG_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/libjpeg
JPEG_CONFIG_DIR := $(JPEG_BUILD_DIR)/include
JPEG_CONFIG_HEADER := $(JPEG_CONFIG_DIR)/jconfig.h
JPEG_CONFIGINT_HEADER := $(JPEG_CONFIG_DIR)/jconfigint.h
JPEG_VERSION_HEADER := $(JPEG_CONFIG_DIR)/jversion.h
FFMPEG_SRC_DIR := $(REPO_ROOT)/Vendor/Library/ffmpeg
FFMPEG_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/ffmpeg
FFMPEG_CONFIGURE := $(FFMPEG_SRC_DIR)/configure
FFMPEG_AVAILABLE := $(wildcard $(FFMPEG_CONFIGURE))
SDL_DIR := SDL
SDL_PORT_DIR := $(REPO_ROOT)/Userland/Service/SDL_ImplusOS
SDL_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/SDL
SDL_CONFIG_DIR := $(SDL_BUILD_DIR)/include
LIB_Z := $(LIB_DIR)/libz.a
LIB_PNG := $(LIB_DIR)/libpng.a
LIB_JPEG := $(LIB_DIR)/libjpeg.a
LIB_SDL := $(LIB_DIR)/libSDL3.a
LIB_AVUTIL := $(LIB_DIR)/libavutil.a
LIB_AVCODEC := $(LIB_DIR)/libavcodec.a
LIB_AVFORMAT := $(LIB_DIR)/libavformat.a
LIB_SWSCALE := $(LIB_DIR)/libswscale.a

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-elf-
ARCH_CFLAGS := -mstrict-align -mno-outline-atomics
else ifeq ($(ARCH),x86_64)
CROSS_COMPILE ?= x86_64-elf-
ARCH_CFLAGS := -mcmodel=large -mno-red-zone
else
$(error Unsupported ARCH '$(ARCH)'. Use x86_64 or arm64.)
endif

ifeq ($(ARCH),arm64)
FFMPEG_ARCH := aarch64
else ifeq ($(ARCH),x86_64)
FFMPEG_ARCH := x86_64
endif

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar

ZLIB_SRCS := adler32.c compress.c crc32.c deflate.c infback.c inffast.c inflate.c inftrees.c trees.c uncompr.c zutil.c
ZLIB_OBJS := $(ZLIB_SRCS:%.c=$(ZLIB_BUILD_DIR)/%.o)

LIBPNG_SRCS := png.c pngerror.c pngget.c pngmem.c pngpread.c pngread.c pngrio.c pngrtran.c pngrutil.c pngset.c pngtrans.c pngwio.c pngwrite.c pngwtran.c pngwutil.c pngsimd.c
LIBPNG_OBJS := $(LIBPNG_SRCS:%.c=$(PNG_BUILD_DIR)/%.o)

LIBJPEG_SRCS := \
	src/jcapimin.c \
	src/wrapper/jcapistd-8.c src/wrapper/jcapistd-12.c src/wrapper/jcapistd-16.c \
	src/wrapper/jccoefct-8.c src/wrapper/jccoefct-12.c \
	src/wrapper/jccolor-8.c src/wrapper/jccolor-12.c src/wrapper/jccolor-16.c \
	src/wrapper/jcdctmgr-8.c src/wrapper/jcdctmgr-12.c \
	src/wrapper/jcdiffct-8.c src/wrapper/jcdiffct-12.c src/wrapper/jcdiffct-16.c \
	src/jchuff.c src/jcicc.c src/jcinit.c src/jclhuff.c \
	src/wrapper/jclossls-8.c src/wrapper/jclossls-12.c src/wrapper/jclossls-16.c \
	src/wrapper/jcmainct-8.c src/wrapper/jcmainct-12.c src/wrapper/jcmainct-16.c \
	src/jcmarker.c src/jcmaster.c src/jcomapi.c src/jcparam.c src/jcphuff.c \
	src/wrapper/jcprepct-8.c src/wrapper/jcprepct-12.c src/wrapper/jcprepct-16.c \
	src/wrapper/jcsample-8.c src/wrapper/jcsample-12.c src/wrapper/jcsample-16.c \
	src/jctrans.c src/jdapimin.c \
	src/wrapper/jdapistd-8.c src/wrapper/jdapistd-12.c src/wrapper/jdapistd-16.c \
	src/jdatadst.c src/jdatasrc.c \
	src/wrapper/jdcoefct-8.c src/wrapper/jdcoefct-12.c \
	src/wrapper/jdcolor-8.c src/wrapper/jdcolor-12.c src/wrapper/jdcolor-16.c \
	src/wrapper/jddctmgr-8.c src/wrapper/jddctmgr-12.c \
	src/wrapper/jddiffct-8.c src/wrapper/jddiffct-12.c src/wrapper/jddiffct-16.c \
	src/jdhuff.c src/jdicc.c src/jdinput.c src/jdlhuff.c \
	src/wrapper/jdlossls-8.c src/wrapper/jdlossls-12.c src/wrapper/jdlossls-16.c \
	src/wrapper/jdmainct-8.c src/wrapper/jdmainct-12.c src/wrapper/jdmainct-16.c \
	src/jdmarker.c src/jdmaster.c \
	src/wrapper/jdmerge-8.c src/wrapper/jdmerge-12.c \
	src/jdphuff.c \
	src/wrapper/jdpostct-8.c src/wrapper/jdpostct-12.c src/wrapper/jdpostct-16.c \
	src/wrapper/jdsample-8.c src/wrapper/jdsample-12.c src/wrapper/jdsample-16.c \
	src/jdtrans.c src/jerror.c src/jfdctflt.c \
	src/wrapper/jfdctfst-8.c src/wrapper/jfdctfst-12.c \
	src/wrapper/jfdctint-8.c src/wrapper/jfdctint-12.c \
	src/wrapper/jidctflt-8.c src/wrapper/jidctflt-12.c \
	src/wrapper/jidctfst-8.c src/wrapper/jidctfst-12.c \
	src/wrapper/jidctint-8.c src/wrapper/jidctint-12.c \
	src/wrapper/jidctred-8.c src/wrapper/jidctred-12.c \
	src/jmemmgr.c src/jmemnobs.c src/jpeg_nbits.c \
	src/wrapper/jquant1-8.c src/wrapper/jquant1-12.c \
	src/wrapper/jquant2-8.c src/wrapper/jquant2-12.c \
	src/wrapper/jutils-8.c src/wrapper/jutils-12.c src/wrapper/jutils-16.c \
	src/jaricom.c src/jcarith.c src/jdarith.c
LIBJPEG_OBJS := $(LIBJPEG_SRCS:%.c=$(JPEG_BUILD_DIR)/%.o)

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

COMMON_CFLAGS := -ffreestanding -fPIC -fno-builtin $(ARCH_CFLAGS) -nostdlib -Os -g0 -ffunction-sections -fdata-sections -I$(REPO_ROOT)/libc/I_libc/include -Wno-error -include stddef.h -include math.h
FFMPEG_CFLAGS := -ffreestanding -fno-pic -fno-builtin $(ARCH_CFLAGS) -nostdlib -O2 -g0 -ffunction-sections -fdata-sections -I$(REPO_ROOT)/libc/I_libc/include -Wno-error -fno-tree-vectorize
ZLIB_CFLAGS := $(COMMON_CFLAGS) -I. -Izlib -DNO_GZFILE_OPS
LIBPNG_CFLAGS := $(COMMON_CFLAGS) -I$(PNG_CONFIG_DIR) -Ilibpng -Izlib
LIBJPEG_CFLAGS := $(COMMON_CFLAGS) -I$(JPEG_CONFIG_DIR) -Ilibjpeg/src -Ilibjpeg/src/wrapper -DC_ARITH_CODING_SUPPORTED=1 -DD_ARITH_CODING_SUPPORTED=1 -Wno-unused-parameter -Wno-sign-conversion -Wno-conversion -Wno-shadow
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

ifneq ($(FFMPEG_AVAILABLE),)
FFMPEG_TARGETS := $(LIB_AVUTIL) $(LIB_AVCODEC) $(LIB_AVFORMAT) $(LIB_SWSCALE)
else
FFMPEG_TARGETS :=
endif

all: $(LIB_Z) $(LIB_PNG) $(LIB_JPEG) $(LIB_SDL) $(FFMPEG_TARGETS)

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

$(JPEG_CONFIG_HEADER):
	@mkdir -p $(dir $@)
	@printf '%s\n' \
		'#pragma once' \
		'#define JPEG_LIB_VERSION 62' \
		'#define LIBJPEG_TURBO_VERSION 3.1.4' \
		'#define LIBJPEG_TURBO_VERSION_NUMBER 3001004' \
		'#define C_ARITH_CODING_SUPPORTED 1' \
		'#define D_ARITH_CODING_SUPPORTED 1' \
		'#define MEM_SRCDST_SUPPORTED 1' \
		'#ifndef BITS_IN_JSAMPLE' \
		'#define BITS_IN_JSAMPLE 8' \
		'#endif' \
		> $@

$(JPEG_CONFIGINT_HEADER):
	@mkdir -p $(dir $@)
	@printf '%s\n' \
		'#pragma once' \
		'#define BUILD "implusos"' \
		'#define HIDDEN __attribute__((visibility("hidden")))' \
		'#define INLINE inline __attribute__((always_inline))' \
		'#define THREAD_LOCAL __thread' \
		'#define PACKAGE_NAME "libjpeg-turbo"' \
		'#define VERSION "3.1.4"' \
		'#define SIZEOF_SIZE_T 8' \
		'#define HAVE_BUILTIN_CTZL 1' \
		'#define FALLTHROUGH __attribute__((fallthrough));' \
		'#ifndef BITS_IN_JSAMPLE' \
		'#define BITS_IN_JSAMPLE 8' \
		'#endif' \
		'#if BITS_IN_JSAMPLE == 8' \
		'#define C_ARITH_CODING_SUPPORTED 1' \
		'#define D_ARITH_CODING_SUPPORTED 1' \
		'#endif' \
		> $@

$(JPEG_VERSION_HEADER): libjpeg/src/jversion.h.in
	@mkdir -p $(dir $@)
	sed -e 's/@COPYRIGHT_YEAR@/1991-2026/g' $< > $@

$(LIB_JPEG): $(LIBJPEG_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(JPEG_BUILD_DIR)/%.o: libjpeg/%.c $(JPEG_CONFIG_HEADER) $(JPEG_CONFIGINT_HEADER) $(JPEG_VERSION_HEADER)
	@mkdir -p $(dir $@)
	$(CC) $(LIBJPEG_CFLAGS) -c $< -o $@

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

$(FFMPEG_BUILD_DIR)/ffbuild/config.mak: $(FFMPEG_CONFIGURE)
	@mkdir -p $(FFMPEG_BUILD_DIR)
	cd $(FFMPEG_BUILD_DIR) && \
		$(FFMPEG_CONFIGURE) \
			--prefix="$(FFMPEG_BUILD_DIR)/install" \
			--disable-programs \
			--disable-doc \
			--disable-debug \
			--disable-shared \
			--enable-static \
			--disable-autodetect \
			--disable-network \
			--disable-pthreads \
			--disable-w32threads \
			--disable-os2threads \
			--enable-cross-compile \
			--target-os=none \
			--arch=$(FFMPEG_ARCH) \
			--cross-prefix="$(CROSS_COMPILE)" \
			--disable-everything \
			--enable-avcodec \
			--enable-avformat \
			--enable-avutil \
			--enable-swscale \
			--enable-decoder='aac,rawvideo,mpeg4,mpeg2video,h264,hevc,vp8,vp9,vc1,wmv3,mjpeg,png,bmp,tiff,gif,webp,flv1,svq1,rv10,rv20,rv30,rv40,fraps,huffyuv,dvvideo,msrle,msvideo1,vp6,vp7,wmv1,wmv2,qtrle,rpza,smc,flashsv,mdec,zlib,zmbv' \
			--enable-demuxer='avi,mov,mp4,matroska,webm,flv,mpegts,mpegps,image2,gif,wav,aac,ogg,asf' \
			--enable-parser='h264,hevc,vp8,vp9,mpeg4video,mpegaudio,aac,ac3,vorbis' \
			--enable-protocol=file \
			--enable-bsf='h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc' \
			--disable-asm \
			--disable-inline-asm \
			--cc="$(CC)" \
			--as="$(CC)" \
			--ar="$(AR)" \
			--nm="$(CROSS_COMPILE)nm" \
			--ranlib="$(CROSS_COMPILE)ranlib" \
			--strip="$(CROSS_COMPILE)strip" \
		--pkg-config=false \
		--extra-cflags="$(FFMPEG_CFLAGS) -DIMPLUSOS_FFMPEG_BUILD -DMALLOC_PREFIX=FFmpeg_ -O2" \
		--extra-ldflags="$(COMMON_CFLAGS)"


$(LIB_AVUTIL) $(LIB_AVCODEC) $(LIB_AVFORMAT) $(LIB_SWSCALE): $(FFMPEG_BUILD_DIR)/ffbuild/config.mak
	MAKEFLAGS= MAKEOVERRIDES= $(MAKE) -C $(FFMPEG_BUILD_DIR) -j1 libavutil/libavutil.a libavcodec/libavcodec.a libavformat/libavformat.a libswscale/libswscale.a
	@mkdir -p $(LIB_DIR)
	@cp $(FFMPEG_BUILD_DIR)/libavutil/libavutil.a $(LIB_AVUTIL)
	@cp $(FFMPEG_BUILD_DIR)/libavcodec/libavcodec.a $(LIB_AVCODEC)
	@cp $(FFMPEG_BUILD_DIR)/libavformat/libavformat.a $(LIB_AVFORMAT)
	@cp $(FFMPEG_BUILD_DIR)/libswscale/libswscale.a $(LIB_SWSCALE)

clean:
	rm -rf $(ZLIB_BUILD_DIR) $(PNG_BUILD_DIR) $(JPEG_BUILD_DIR) $(SDL_BUILD_DIR) $(FFMPEG_BUILD_DIR) $(LIB_Z) $(LIB_PNG) $(LIB_JPEG) $(LIB_SDL) $(LIB_AVUTIL) $(LIB_AVCODEC) $(LIB_AVFORMAT) $(LIB_SWSCALE)
