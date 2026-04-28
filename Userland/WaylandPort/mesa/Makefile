PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/mesa
BUILDDIR := $(PORTDIR)/build/mesa
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR)/include -I$(SRCDIR)/src -I$(SRCDIR)/src/mesa \
    -I$(SRCDIR)/src/mapi -I$(SRCDIR)/src/gallium/include \
    -I$(SRCDIR)/src/gallium/auxiliary \
    -I$(INSTDIR)/include -I$(INSTDIR)/include/libdrm \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include -I$(PORTDIR)/mesa \
    -DPACKAGE_VERSION=\"24.0\" -DPACKAGE_BUGREPORT=\"\" \
    -DHAVE_PTHREAD=0 -DHAVE_DLOPEN=0 -DHAVE_LIBDRM=1 \
    -DGALLIUM_SOFTPIPE=1 -DGALLIUM_LLVMPIPE=0 \
    -DUSE_SSE41=0 -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS \
    -D__STDC_LIMIT_MACROS -DNDEBUG

SWRAST_SRCS := \
    src/mesa/main/accum.c src/mesa/main/api_arrayelt.c \
    src/mesa/main/blend.c src/mesa/main/bufferobj.c \
    src/mesa/main/context.c src/mesa/main/debug.c \
    src/mesa/main/depth.c src/mesa/main/draw.c \
    src/mesa/main/enable.c src/mesa/main/errors.c \
    src/mesa/main/extensions.c src/mesa/main/fbobject.c \
    src/mesa/main/fog.c src/mesa/main/format_utils.c \
    src/mesa/main/formats.c src/mesa/main/framebuffer.c \
    src/mesa/main/glformats.c src/mesa/main/light.c \
    src/mesa/main/matrix.c src/mesa/main/mipmap.c \
    src/mesa/main/pixel.c src/mesa/main/pixelstore.c \
    src/mesa/main/renderbuffer.c src/mesa/main/scissor.c \
    src/mesa/main/shaderobj.c src/mesa/main/state.c \
    src/mesa/main/stencil.c src/mesa/main/teximage.c \
    src/mesa/main/texobj.c src/mesa/main/texstate.c \
    src/mesa/main/viewport.c src/mesa/main/version.c

SWRAST_OBJS := $(addprefix $(BUILDDIR)/, $(notdir $(SWRAST_SRCS:.c=.o)))

EGL_SRCS := \
    src/egl/main/eglapi.c src/egl/main/eglarray.c \
    src/egl/main/eglconfig.c src/egl/main/eglcontext.c \
    src/egl/main/eglcurrent.c src/egl/main/egldisplay.c \
    src/egl/main/egldriver.c src/egl/main/eglglobals.c \
    src/egl/main/eglimage.c src/egl/main/egllog.c \
    src/egl/main/eglsurface.c src/egl/main/eglsync.c

EGL_OBJS := $(addprefix $(BUILDDIR)/egl_, $(notdir $(EGL_SRCS:.c=.o)))

.PHONY: all install clean

all: $(BUILDDIR)/libGL.a $(BUILDDIR)/libEGL.a install

$(BUILDDIR)/libGL.a: $(SWRAST_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/libEGL.a: $(EGL_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/%.o: $(SRCDIR)/src/mesa/main/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/egl_%.o: $(SRCDIR)/src/egl/main/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libGL.a $(BUILDDIR)/libEGL.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include/GL $(INSTDIR)/include/EGL $(INSTDIR)/include/KHR
	cp $(BUILDDIR)/libGL.a $(INSTDIR)/lib/
	cp $(BUILDDIR)/libEGL.a $(INSTDIR)/lib/
	cp $(SRCDIR)/include/GL/*.h $(INSTDIR)/include/GL/ 2>/dev/null || true
	cp $(SRCDIR)/include/EGL/*.h $(INSTDIR)/include/EGL/ 2>/dev/null || true
	cp $(SRCDIR)/include/KHR/*.h $(INSTDIR)/include/KHR/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
