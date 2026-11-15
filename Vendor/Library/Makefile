ARCH ?= x86_64
REPO_ROOT := $(abspath ../..)
TOP_BUILD_DIR ?= $(REPO_ROOT)/Build/$(ARCH)
LIB_DIR := $(TOP_BUILD_DIR)/Vendor/Library
FREETYPE_SRC_DIR := $(REPO_ROOT)/Vendor/Library/freetype
FREETYPE_BUILD_DIR := $(TOP_BUILD_DIR)/Vendor/Library/freetype
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

COMMON_CFLAGS := -ffreestanding -fPIC -fno-builtin $(ARCH_CFLAGS) -nostdlib -Os -g0 -ffunction-sections -fdata-sections -I$(REPO_ROOT)/libc/I_libc/Source/include -Wno-error -include stddef.h -include math.h
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

all: $(LIB_FREETYPE)

$(LIB_FREETYPE): $(FREETYPE_OBJS)
	@mkdir -p $(LIB_DIR)
	$(AR) rcs $@ $^

$(FREETYPE_BUILD_DIR)/%.o: $(FREETYPE_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(FREETYPE_CFLAGS) -c $< -o $@

clean:
	rm -rf $(FREETYPE_BUILD_DIR) $(LIB_FREETYPE)
