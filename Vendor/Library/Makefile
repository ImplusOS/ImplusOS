ARCH ?= x86_64
REPO_ROOT := $(abspath ../..)
TOP_BUILD_DIR ?= $(REPO_ROOT)/Build/$(ARCH)
LIB_DIR := $(TOP_BUILD_DIR)/Vendor/Library
ZLIB_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/zlib
PNG_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/libpng
PNG_CONFIG_DIR := $(PNG_BUILD_DIR)/include
PNG_CONFIG_HEADER := $(PNG_CONFIG_DIR)/pnglibconf.h
LIB_Z := $(LIB_DIR)/libz.a
LIB_PNG := $(LIB_DIR)/libpng.a

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

COMMON_CFLAGS := -ffreestanding -fno-pic -fno-builtin $(ARCH_CFLAGS) -nostdlib -O2 -I$(REPO_ROOT)/libc/I_libc/include -Wno-error -include stddef.h
ZLIB_CFLAGS := $(COMMON_CFLAGS) -I. -Izlib -DNO_GZFILE_OPS
LIBPNG_CFLAGS := $(COMMON_CFLAGS) -I$(PNG_CONFIG_DIR) -Ilibpng -Izlib

all: $(LIB_Z) $(LIB_PNG)

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

clean:
	rm -rf $(ZLIB_BUILD_DIR) $(PNG_BUILD_DIR) $(LIB_Z) $(LIB_PNG)
