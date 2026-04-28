###############################################################################
# NetSurf for ImplusOS — Unified Build
#
# Cross-compiles NetSurf + all dependency libraries for ImplusOS
# using x86_64-elf-gcc in freestanding mode.
###############################################################################

CC  := x86_64-elf-gcc
CXX := x86_64-elf-g++
LD  := x86_64-elf-ld
AR  := x86_64-elf-ar

APP_NAME  := netsurf
SRC_BASE  := .
NS_DIR    := netsurf
LIBS_DIR  := libs
COMPAT_DIR := compat
BUILD_DIR := ../../../../Build/Userland/UserApps/$(APP_NAME)
ELF       := $(BUILD_DIR)/$(APP_NAME).ELF

include ../../../AppCommon.mk

# ============================================================================
# Compiler Flags
# ============================================================================

WARN_FLAGS := -Wall -Wno-unused-parameter -Wno-unused-variable \
              -Wno-sign-compare -Wno-missing-field-initializers \
              -Wno-implicit-function-declaration -Wno-pointer-sign \
              -Wno-unused-function -Wno-missing-prototypes \
              -Wno-strict-prototypes -Wno-nested-externs \
              -Wno-old-style-definition -Wno-type-limits \
              -Wno-conversion -Wno-sign-conversion -Wno-shadow \
              -Wno-missing-declarations

CFLAGS := -std=c11 \
          -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
          -mcmodel=large -mno-red-zone -nostdlib -nostartfiles -nodefaultlibs \
          -O2 -g \
          -U_FORTIFY_SOURCE -D_ALIGNED= \
          $(WARN_FLAGS) \
          -MMD -MP

# Base include paths (shared by all)
BASE_INCLUDES := -I../../../../libc/include \
          -I../../../../libc/include/sys \
          -I../../../../Userland \
          -I../../../../Userland/API \
          -I../../../../Userland/POSIX/include \
          -I../../../../Userland/MuslPort/include \
          -I../../../../musl/include \
          -I../../../../musl/arch/x86_64 \
          -I../../../../musl/arch/generic \
          -I$(COMPAT_DIR) \
          -I$(LIBS_DIR)/libparserutils/include \
          -I$(LIBS_DIR)/libwapcaplet/include \
          -I$(LIBS_DIR)/libhubbub/include \
          -I$(LIBS_DIR)/libcss/include \
          -I$(LIBS_DIR)/libdom/include \
          -I$(LIBS_DIR)/libnsutils/include \
          -I$(LIBS_DIR)/libnsfb/include \
          -I$(LIBS_DIR)/libnsbmp/include \
          -I$(LIBS_DIR)/libnsgif/include \
          -I$(LIBS_DIR)/libnslog/include \
          -I$(LIBS_DIR)/libnspsl/include \
          -I$(LIBS_DIR)/freetype/include \
          -I$(LIBS_DIR)/zlib \
          -I$(LIBS_DIR)/libdom/bindings

# Library internal include paths (for NetSurf core that needs all of them)
LIB_INCLUDES := -I$(LIBS_DIR)/libcss/src \
          -I$(LIBS_DIR)/libdom/src \
          -I$(LIBS_DIR)/libhubbub/src \
          -I$(LIBS_DIR)/libparserutils/src \
          -I$(LIBS_DIR)/libnsfb/src

# NetSurf core include paths
NS_INCLUDES := -I$(NS_DIR) \
          -I$(NS_DIR)/include \
          -I$(NS_DIR)/frontends \
          -I$(NS_DIR)/content/handlers

# Build configuration defines
NS_DEFINES := -DNETSURF_HOMEPAGE='"about:welcome"' \
          -DNETSURF_LOG_LEVEL=WARNING \
          -DNETSURF_UA_FORMAT_STRING='"Mozilla/5.0 (%s) NetSurf/%d.%d"' \
          -DNETSURF_BUILTIN_LOG_FILTER='"(level:WARNING)"' \
          -DNETSURF_BUILTIN_VERBOSE_FILTER='"(level:VERBOSE)"' \
          -DSTMTEXPR=1 -Dnsimplusos -Dsmall -DNO_IPV6 \
          -UWITH_JPEG -UWITH_PNG -UWITH_WEBP -UWITH_NS_SVG -UWITH_JPEGXL \
          -UWITH_OPENSSL -UWITH_PDF_EXPORT \
          -DLIBICONV_PLUG -DFB_USE_FREETYPE \
          -DWITH_BMP -DWITH_GIF

FT_DEFINES := -DFT2_BUILD_LIBRARY -DFT_CONFIG_OPTION_SYSTEM_ZLIB -DFT_CONFIG_OPTION_NO_ASSEMBLER

# CFLAGS for library sources (lib internal includes FIRST to avoid collision)
CFLAGS_LIB := $(CFLAGS) $(NS_DEFINES) $(FT_DEFINES) $(LIB_INCLUDES) $(BASE_INCLUDES)

# CFLAGS for NetSurf core sources (force-include compat.h for POSIX constants)
CFLAGS_NS := $(CFLAGS) $(NS_DEFINES) -include $(COMPAT_DIR)/compat.h $(NS_INCLUDES) $(LIB_INCLUDES) $(BASE_INCLUDES)

# CFLAGS for compat/frontend sources
CFLAGS_COMPAT := $(CFLAGS) $(NS_DEFINES) $(NS_INCLUDES) $(LIB_INCLUDES) $(BASE_INCLUDES)

LDFLAGS := -T ../../../../Userland/Userland.ld -e _start -nostdlib --build-id=none

# ============================================================================
# Source Files
# ============================================================================

# --- Compatibility shim ---
COMPAT_SRCS := $(COMPAT_DIR)/compat.c $(COMPAT_DIR)/nslog_stub.c

# --- libparserutils ---
LIBPARSERUTILS_SRCS := \
    $(LIBS_DIR)/libparserutils/src/charset/aliases.c \
    $(LIBS_DIR)/libparserutils/src/charset/codec.c \
    $(LIBS_DIR)/libparserutils/src/charset/codecs/codec_8859.c \
    $(LIBS_DIR)/libparserutils/src/charset/codecs/codec_ascii.c \
    $(LIBS_DIR)/libparserutils/src/charset/codecs/codec_ext8.c \
    $(LIBS_DIR)/libparserutils/src/charset/codecs/codec_utf16.c \
    $(LIBS_DIR)/libparserutils/src/charset/codecs/codec_utf8.c \
    $(LIBS_DIR)/libparserutils/src/charset/encodings/utf16.c \
    $(LIBS_DIR)/libparserutils/src/charset/encodings/utf8.c \
    $(LIBS_DIR)/libparserutils/src/input/filter.c \
    $(LIBS_DIR)/libparserutils/src/input/inputstream.c \
    $(LIBS_DIR)/libparserutils/src/utils/buffer.c \
    $(LIBS_DIR)/libparserutils/src/utils/errors.c \
    $(LIBS_DIR)/libparserutils/src/utils/stack.c \
    $(LIBS_DIR)/libparserutils/src/utils/vector.c

# --- libwapcaplet ---
LIBWAPCAPLET_SRCS := \
    $(LIBS_DIR)/libwapcaplet/src/libwapcaplet.c

# --- libnsutils ---
LIBNSUTILS_SRCS := \
    $(LIBS_DIR)/libnsutils/src/base64.c \
    $(LIBS_DIR)/libnsutils/src/time.c \
    $(LIBS_DIR)/libnsutils/src/unistd.c

# --- libhubbub ---
LIBHUBBUB_SRCS := \
    $(LIBS_DIR)/libhubbub/src/charset/detect.c \
    $(LIBS_DIR)/libhubbub/src/parser.c \
    $(LIBS_DIR)/libhubbub/src/tokeniser/entities.c \
    $(LIBS_DIR)/libhubbub/src/tokeniser/tokeniser.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/after_after_body.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/after_after_frameset.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/after_body.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/after_frameset.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/after_head.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/before_head.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/before_html.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/element-type.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/generic_rcdata.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_body.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_caption.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_cell.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_column_group.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_foreign_content.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_frameset.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_head.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_head_noscript.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_row.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_select.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_select_in_table.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_table.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/in_table_body.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/initial.c \
    $(LIBS_DIR)/libhubbub/src/treebuilder/treebuilder.c \
    $(LIBS_DIR)/libhubbub/src/utils/errors.c \
    $(LIBS_DIR)/libhubbub/src/utils/string.c

# --- libcss ---
LIBCSS_SRCS := $(shell find $(LIBS_DIR)/libcss/src -name '*.c' | sort)

# --- libdom ---
LIBDOM_SRCS := $(shell find $(LIBS_DIR)/libdom/src -name '*.c' | sort) \
               $(shell find $(LIBS_DIR)/libdom/bindings/hubbub -name '*.c' | sort)

# --- libnsfb (only RAM surface + 32bpp plotter, no SDL/X11/VNC) ---
LIBNSFB_SRCS := \
    $(LIBS_DIR)/libnsfb/src/cursor.c \
    $(LIBS_DIR)/libnsfb/src/dump.c \
    $(LIBS_DIR)/libnsfb/src/libnsfb.c \
    $(LIBS_DIR)/libnsfb/src/palette.c \
    $(LIBS_DIR)/libnsfb/src/plot/api.c \
    $(LIBS_DIR)/libnsfb/src/plot/common.c \
    $(LIBS_DIR)/libnsfb/src/plot/generic.c \
    $(LIBS_DIR)/libnsfb/src/plot/util.c \
    $(LIBS_DIR)/libnsfb/src/plot/32bpp-common.c \
    $(LIBS_DIR)/libnsfb/src/plot/32bpp-xrgb8888.c \
    $(LIBS_DIR)/libnsfb/src/plot/32bpp-xbgr8888.c \
    $(LIBS_DIR)/libnsfb/src/surface/ram.c \
    $(LIBS_DIR)/libnsfb/src/surface/surface.c

# --- libnsbmp ---
LIBNSBMP_SRCS := $(LIBS_DIR)/libnsbmp/src/libnsbmp.c

# --- libnsgif ---
LIBNSGIF_SRCS := \
    $(LIBS_DIR)/libnsgif/src/gif.c \
    $(LIBS_DIR)/libnsgif/src/lzw.c

# --- libnslog (stub - real one needs bison/flex) ---
LIBNSLOG_SRCS :=

# --- libnspsl ---
LIBNSPSL_SRCS := $(LIBS_DIR)/libnspsl/src/nspsl.c

# --- zlib (core files only) ---
ZLIB_SRCS := \
    $(LIBS_DIR)/zlib/adler32.c \
    $(LIBS_DIR)/zlib/crc32.c \
    $(LIBS_DIR)/zlib/deflate.c \
    $(LIBS_DIR)/zlib/inffast.c \
    $(LIBS_DIR)/zlib/inflate.c \
    $(LIBS_DIR)/zlib/inftrees.c \
    $(LIBS_DIR)/zlib/trees.c \
    $(LIBS_DIR)/zlib/zutil.c \
    $(LIBS_DIR)/zlib/compress.c \
    $(LIBS_DIR)/zlib/uncompr.c \
    $(LIBS_DIR)/zlib/gzlib.c \
    $(LIBS_DIR)/zlib/gzread.c \
    $(LIBS_DIR)/zlib/gzwrite.c \
    $(LIBS_DIR)/zlib/gzclose.c

# --- FreeType2 (minimal set for TrueType rendering) ---
FREETYPE_SRCS := \
    $(LIBS_DIR)/freetype/src/base/ftsystem.c \
    $(LIBS_DIR)/freetype/src/base/ftinit.c \
    $(LIBS_DIR)/freetype/src/base/ftdebug.c \
    $(LIBS_DIR)/freetype/src/base/ftbase.c \
    $(LIBS_DIR)/freetype/src/base/ftbbox.c \
    $(LIBS_DIR)/freetype/src/base/ftbitmap.c \
    $(LIBS_DIR)/freetype/src/base/ftmm.c \
    $(LIBS_DIR)/freetype/src/base/ftglyph.c \
    $(LIBS_DIR)/freetype/src/base/ftstroke.c \
    $(LIBS_DIR)/freetype/src/base/ftsynth.c \
    $(LIBS_DIR)/freetype/src/truetype/truetype.c \
    $(LIBS_DIR)/freetype/src/smooth/smooth.c \
    $(LIBS_DIR)/freetype/src/raster/raster.c \
    $(LIBS_DIR)/freetype/src/cff/cff.c \
    $(LIBS_DIR)/freetype/src/sfnt/sfnt.c \
    $(LIBS_DIR)/freetype/src/autofit/autofit.c \
    $(LIBS_DIR)/freetype/src/psnames/psnames.c \
    $(LIBS_DIR)/freetype/src/psaux/psaux.c \
    $(LIBS_DIR)/freetype/src/pshinter/pshinter.c \
    $(LIBS_DIR)/freetype/src/gzip/ftgzip.c

# --- NetSurf Core Sources ---

# Utils
NS_UTILS_SRCS := \
    $(NS_DIR)/utils/bloom.c \
    $(NS_DIR)/utils/corestrings.c \
    $(NS_DIR)/utils/file.c \
    $(NS_DIR)/utils/filepath.c \
    $(NS_DIR)/utils/hashmap.c \
    $(NS_DIR)/utils/hashtable.c \
    $(NS_DIR)/utils/idna.c \
    $(NS_DIR)/utils/libdom.c \
    $(NS_DIR)/utils/log.c \
    $(NS_DIR)/utils/messages.c \
    $(NS_DIR)/utils/nscolour.c \
    $(NS_DIR)/utils/nsoption.c \
    $(NS_DIR)/utils/nsurl/nsurl.c \
    $(NS_DIR)/utils/nsurl/parse.c \
    $(NS_DIR)/utils/punycode.c \
    $(NS_DIR)/utils/ssl_certs.c \
    $(NS_DIR)/utils/talloc.c \
    $(NS_DIR)/utils/time.c \
    $(NS_DIR)/utils/url.c \
    $(NS_DIR)/utils/useragent.c \
    $(NS_DIR)/utils/utf8.c \
    $(NS_DIR)/utils/utils.c

# HTTP utils
NS_HTTP_SRCS := \
    $(NS_DIR)/utils/http/cache-control.c \
    $(NS_DIR)/utils/http/challenge.c \
    $(NS_DIR)/utils/http/content-disposition.c \
    $(NS_DIR)/utils/http/content-type.c \
    $(NS_DIR)/utils/http/generics.c \
    $(NS_DIR)/utils/http/parameter.c \
    $(NS_DIR)/utils/http/primitives.c \
    $(NS_DIR)/utils/http/strict-transport-security.c \
    $(NS_DIR)/utils/http/www-authenticate.c

# Desktop
NS_DESKTOP_SRCS := \
    $(NS_DIR)/desktop/bitmap.c \
    $(NS_DIR)/desktop/browser.c \
    $(NS_DIR)/desktop/browser_history.c \
    $(NS_DIR)/desktop/browser_window.c \
    $(NS_DIR)/desktop/cookie_manager.c \
    $(NS_DIR)/desktop/cw_helper.c \
    $(NS_DIR)/desktop/download.c \
    $(NS_DIR)/desktop/frames.c \
    $(NS_DIR)/desktop/global_history.c \
    $(NS_DIR)/desktop/gui_factory.c \
    $(NS_DIR)/desktop/hotlist.c \
    $(NS_DIR)/desktop/knockout.c \
    $(NS_DIR)/desktop/local_history.c \
    $(NS_DIR)/desktop/mouse.c \
    $(NS_DIR)/desktop/netsurf.c \
    $(NS_DIR)/desktop/page-info.c \
    $(NS_DIR)/desktop/plot_style.c \
    $(NS_DIR)/desktop/print.c \
    $(NS_DIR)/desktop/save_complete.c \
    $(NS_DIR)/desktop/save_text.c \
    $(NS_DIR)/desktop/scrollbar.c \
    $(NS_DIR)/desktop/search.c \
    $(NS_DIR)/desktop/searchweb.c \
    $(NS_DIR)/desktop/selection.c \
    $(NS_DIR)/desktop/system_colour.c \
    $(NS_DIR)/desktop/textarea.c \
    $(NS_DIR)/desktop/textinput.c \
    $(NS_DIR)/desktop/treeview.c \
    $(NS_DIR)/desktop/version.c

# Content
NS_CONTENT_SRCS := \
    $(NS_DIR)/content/content.c \
    $(NS_DIR)/content/content_factory.c \
    $(NS_DIR)/content/fetch.c \
    $(NS_DIR)/content/hlcache.c \
    $(NS_DIR)/content/llcache.c \
    $(NS_DIR)/content/mimesniff.c \
    $(NS_DIR)/content/no_backing_store.c \
    $(NS_DIR)/content/textsearch.c \
    $(NS_DIR)/content/urldb.c

# Content fetchers (disable curl, keep data/resource/file/about)
NS_FETCHER_SRCS := \
    $(NS_DIR)/content/fetchers/data.c \
    $(NS_DIR)/content/fetchers/resource.c \
    $(NS_DIR)/content/fetchers/file/dirlist.c \
    $(NS_DIR)/content/fetchers/file/file.c \
    $(NS_DIR)/content/fetchers/about/about.c \
    $(NS_DIR)/content/fetchers/about/blank.c \
    $(NS_DIR)/content/fetchers/about/certificate.c \
    $(NS_DIR)/content/fetchers/about/chart.c \
    $(NS_DIR)/content/fetchers/about/choices.c \
    $(NS_DIR)/content/fetchers/about/config.c \
    $(NS_DIR)/content/fetchers/about/imagecache.c \
    $(NS_DIR)/content/fetchers/about/nscolours.c \
    $(NS_DIR)/content/fetchers/about/query.c \
    $(NS_DIR)/content/fetchers/about/query_auth.c \
    $(NS_DIR)/content/fetchers/about/query_fetcherror.c \
    $(NS_DIR)/content/fetchers/about/query_privacy.c \
    $(NS_DIR)/content/fetchers/about/query_timeout.c \
    $(NS_DIR)/content/fetchers/about/testament.c \
    $(NS_DIR)/content/fetchers/about/websearch.c

# Content handlers
NS_HANDLER_SRCS := \
    $(NS_DIR)/content/handlers/css/css.c \
    $(NS_DIR)/content/handlers/css/dump.c \
    $(NS_DIR)/content/handlers/css/hints.c \
    $(NS_DIR)/content/handlers/css/internal.c \
    $(NS_DIR)/content/handlers/css/select.c \
    $(NS_DIR)/content/handlers/html/box_construct.c \
    $(NS_DIR)/content/handlers/html/box_inspect.c \
    $(NS_DIR)/content/handlers/html/box_manipulate.c \
    $(NS_DIR)/content/handlers/html/box_normalise.c \
    $(NS_DIR)/content/handlers/html/box_special.c \
    $(NS_DIR)/content/handlers/html/box_textarea.c \
    $(NS_DIR)/content/handlers/html/css.c \
    $(NS_DIR)/content/handlers/html/css_fetcher.c \
    $(NS_DIR)/content/handlers/html/dom_event.c \
    $(NS_DIR)/content/handlers/html/font.c \
    $(NS_DIR)/content/handlers/html/form.c \
    $(NS_DIR)/content/handlers/html/forms.c \
    $(NS_DIR)/content/handlers/html/html.c \
    $(NS_DIR)/content/handlers/html/imagemap.c \
    $(NS_DIR)/content/handlers/html/interaction.c \
    $(NS_DIR)/content/handlers/html/layout.c \
    $(NS_DIR)/content/handlers/html/layout_flex.c \
    $(NS_DIR)/content/handlers/html/object.c \
    $(NS_DIR)/content/handlers/html/redraw.c \
    $(NS_DIR)/content/handlers/html/redraw_border.c \
    $(NS_DIR)/content/handlers/html/script.c \
    $(NS_DIR)/content/handlers/html/table.c \
    $(NS_DIR)/content/handlers/html/textselection.c \
    $(NS_DIR)/content/handlers/image/bmp.c \
    $(NS_DIR)/content/handlers/image/gif.c \
    $(NS_DIR)/content/handlers/image/ico.c \
    $(NS_DIR)/content/handlers/image/image.c \
    $(NS_DIR)/content/handlers/image/image_cache.c \
    $(NS_DIR)/content/handlers/javascript/none/none.c \
    $(NS_DIR)/content/handlers/text/textplain.c

# ImplusOS Frontend
NS_FRONTEND_SRCS := \
    $(NS_DIR)/frontends/implusos/gui.c \
    $(NS_DIR)/frontends/implusos/bitmap.c \
    $(NS_DIR)/frontends/implusos/schedule.c \
    $(NS_DIR)/frontends/implusos/fetch.c \
    $(NS_DIR)/frontends/implusos/font_freetype.c

# ============================================================================
# All Sources
# ============================================================================

ALL_SRCS := \
    $(COMPAT_SRCS) \
    $(LIBPARSERUTILS_SRCS) \
    $(LIBWAPCAPLET_SRCS) \
    $(LIBNSUTILS_SRCS) \
    $(LIBHUBBUB_SRCS) \
    $(LIBCSS_SRCS) \
    $(LIBDOM_SRCS) \
    $(LIBNSBMP_SRCS) \
    $(LIBNSGIF_SRCS) \
    $(LIBNSLOG_SRCS) \
    $(LIBNSPSL_SRCS) \
    $(ZLIB_SRCS) \
    $(FREETYPE_SRCS) \
    $(NS_UTILS_SRCS) \
    $(NS_HTTP_SRCS) \
    $(NS_DESKTOP_SRCS) \
    $(NS_CONTENT_SRCS) \
    $(NS_FETCHER_SRCS) \
    $(NS_HANDLER_SRCS) \
    $(NS_FRONTEND_SRCS)

ALL_OBJS := $(ALL_SRCS:%.c=$(BUILD_DIR)/%.o)
OBJS := $(ALL_OBJS) $(COMMON_OBJS)

# ============================================================================
# Build Rules
# ============================================================================

RESOURCE_DIR := Resource

all: $(ELF) copy_resources

copy_resources:
	mkdir -p $(BUILD_DIR)/Resource
	cp -a $(NS_DIR)/resources/* $(BUILD_DIR)/Resource/

# Per-library compile rules: each library gets its own src/ as first -I
$(BUILD_DIR)/$(LIBS_DIR)/libparserutils/%.o: $(LIBS_DIR)/libparserutils/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LIB) -I$(LIBS_DIR)/libparserutils/src -c $< -o $@

$(BUILD_DIR)/$(LIBS_DIR)/libcss/%.o: $(LIBS_DIR)/libcss/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LIB) -I$(LIBS_DIR)/libcss/src -c $< -o $@

$(BUILD_DIR)/$(LIBS_DIR)/libdom/%.o: $(LIBS_DIR)/libdom/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LIB) -I$(LIBS_DIR)/libdom/src -c $< -o $@

$(BUILD_DIR)/$(LIBS_DIR)/libhubbub/%.o: $(LIBS_DIR)/libhubbub/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LIB) -I$(LIBS_DIR)/libhubbub/src -c $< -o $@

$(BUILD_DIR)/$(LIBS_DIR)/libnsfb/%.o: $(LIBS_DIR)/libnsfb/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LIB) -I$(LIBS_DIR)/libnsfb/src -c $< -o $@

# Other libraries (no internal utils/utils.h collision)
$(BUILD_DIR)/$(LIBS_DIR)/%.o: $(LIBS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_LIB) -c $< -o $@

# NetSurf core sources: NS includes first
$(BUILD_DIR)/$(NS_DIR)/%.o: $(NS_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_NS) -c $< -o $@

# Compat sources
$(BUILD_DIR)/$(COMPAT_DIR)/%.o: $(COMPAT_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMPAT) -c $< -o $@

$(ELF): $(OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(ALL_OBJS:.o=.d)

.PHONY: all clean copy_resources
