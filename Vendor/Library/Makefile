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
FREETYPE_SRC_DIR := $(REPO_ROOT)/Vendor/Library/freetype
FREETYPE_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/freetype
LIB_Z := $(LIB_DIR)/libz.a
LIB_PNG := $(LIB_DIR)/libpng.a
LIB_JPEG := $(LIB_DIR)/libjpeg.a
LIB_FREETYPE := $(LIB_DIR)/libfreetype.a

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

COMMON_CFLAGS := -ffreestanding -fPIC -fno-builtin $(ARCH_CFLAGS) -nostdlib -Os -g0 -ffunction-sections -fdata-sections -I$(REPO_ROOT)/libc/I_libc/include -Wno-error -include stddef.h -include math.h
ZLIB_CFLAGS := $(COMMON_CFLAGS) -I. -Izlib -DNO_GZFILE_OPS
LIBPNG_CFLAGS := $(COMMON_CFLAGS) -I$(PNG_CONFIG_DIR) -Ilibpng -Izlib
LIBJPEG_CFLAGS := $(COMMON_CFLAGS) -I$(JPEG_CONFIG_DIR) -Ilibjpeg/src -Ilibjpeg/src/wrapper -DC_ARITH_CODING_SUPPORTED=1 -DD_ARITH_CODING_SUPPORTED=1 -Wno-unused-parameter -Wno-sign-conversion -Wno-conversion -Wno-shadow
FREETYPE_SRCS := \
	src/base/ftbase.c \
	src/base/ftinit.c \
	src/base/ftbbox.c \
	src/base/ftbitmap.c \
	src/base/ftglyph.c \
	src/base/ftstroke.c \
	src/base/ftsynth.c \
	src/base/ftmm.c \
	src/base/fttype1.c \
	src/base/ftwinfnt.c \
	src/base/ftbdf.c \
	src/base/ftcid.c \
	src/base/ftgasp.c \
	src/base/ftgxval.c \
	src/base/ftotval.c \
	src/base/ftpatent.c \
	src/base/ftpfr.c \
	src/base/ftfstype.c \
	src/truetype/truetype.c \
	src/cff/cff.c \
	src/type1/type1.c \
	src/cid/type1cid.c \
	src/type42/type42.c \
	src/sfnt/sfnt.c \
	src/psaux/psaux.c \
	src/pshinter/pshinter.c \
	src/psnames/psnames.c \
	src/smooth/smooth.c \
	src/raster/raster.c \
	src/sdf/sdf.c \
	src/svg/svg.c \
	src/autofit/autofit.c \
	src/cache/ftcache.c \
	src/pfr/pfr.c \
	src/winfonts/winfnt.c \
	src/pcf/pcf.c \
	src/bdf/bdf.c \
	src/hvf/hvf.c \
	src/gzip/ftgzip.c \
	src/lzw/ftlzw.c \
	src/base/ftdebug.c \
	src/base/ftsystem.c \
	src/base/fterrors.c
FREETYPE_OBJS := $(FREETYPE_SRCS:%.c=$(FREETYPE_BUILD_DIR)/%.o)

FREETYPE_CFLAGS := $(COMMON_CFLAGS) \
	-I$(FREETYPE_SRC_DIR)/include \
	-DFT2_BUILD_LIBRARY \
	-Wno-unused-parameter \
	-Wno-sign-conversion \
	-Wno-conversion \
	-Wno-shadow

all: $(LIB_Z) $(LIB_PNG) $(LIB_JPEG) $(LIB_FREETYPE)

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

$(LIB_FREETYPE): $(FREETYPE_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(FREETYPE_BUILD_DIR)/%.o: $(FREETYPE_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FREETYPE_CFLAGS) -c $< -o $@

clean:
	rm -rf $(ZLIB_BUILD_DIR) $(PNG_BUILD_DIR) $(JPEG_BUILD_DIR) $(FREETYPE_BUILD_DIR) $(LIB_Z) $(LIB_PNG) $(LIB_JPEG) $(LIB_FREETYPE)
