ARCH ?= x86_64
REPO_ROOT := $(abspath ../../..)
TOP_BUILD_DIR ?= $(REPO_ROOT)/Build/$(ARCH)

# Shared staging prefix
PREFIX := $(REPO_ROOT)/Userland/Application/UserApps/NetSurf/Root

# Vendor sources
VENDOR := $(REPO_ROOT)/Vendor/Library
ZLIB_DIR := $(VENDOR)/zlib
LIBPNG_DIR := $(VENDOR)/libpng
LIBJPEG_DIR := $(VENDOR)/libjpeg
MBEDTLS_DIR := $(VENDOR)/mbedtls-3.6
CURL_DIR := $(VENDOR)/curl
SDL12_DIR := $(REPO_ROOT)/Userland/Service/SDL12_Compat

# Toolchain
ifeq ($(ARCH),x86_64)
CROSS_COMPILE ?= x86_64-elf-
ARCH_CFLAGS := -mcmodel=large -mno-red-zone
endif
CC := $(shell command -v $(CROSS_COMPILE)gcc 2>/dev/null || printf '%s\n' $(CROSS_COMPILE)gcc)
AR := $(shell command -v $(CROSS_COMPILE)ar 2>/dev/null || printf '%s\n' $(CROSS_COMPILE)ar)
RANLIB := $(shell command -v $(CROSS_COMPILE)ranlib 2>/dev/null || printf '%s\n' $(CROSS_COMPILE)ranlib)
OBJCOPY := $(shell command -v $(CROSS_COMPILE)objcopy 2>/dev/null || printf '%s\n' $(CROSS_COMPILE)objcopy)
BUILD_CC := cc

# Buildsystem location
NSSHARED := $(REPO_ROOT)/Userland/Application/UserApps/NetSurf/netsurf-buildsystem
NETSURF_ROOT_STAMP := $(PREFIX)/.netsurf-root.stamp
ZLIB_PKGCONFIG := $(PREFIX)/lib/pkgconfig/zlib.pc
LIBPNG_PKGCONFIG := $(PREFIX)/lib/pkgconfig/libpng.pc
LIBJPEG_PKGCONFIG := $(PREFIX)/lib/pkgconfig/libjpeg.pc
MBEDTLS_BUILD_DIR := $(TOP_BUILD_DIR)/Userland/Service/NetSurf/mbedtls
CURL_BUILD_DIR := $(TOP_BUILD_DIR)/Userland/Service/NetSurf/curl
LIBM_BUILD_DIR := $(TOP_BUILD_DIR)/Userland/Service/NetSurf/libm
LIBM_OBJ := $(LIBM_BUILD_DIR)/math.o
LIBM := $(PREFIX)/lib/libm.a
LICENSE_DIR := $(PREFIX)/share/licenses

# Common flags for NetSurf library builds
NS_CFLAGS := -ffreestanding -fno-pic -fno-builtin $(ARCH_CFLAGS) -nostdlib -O2 \
	-include stdio.h \
	-I$(REPO_ROOT)/libc/I_libc/include \
	-I$(PREFIX)/include \
	-D_BSD_SOURCE -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L \
	-Wno-unused-parameter -Wno-unused-function

# NetSurf uses a small fixed fetcher pool on ImplusOS; libcurl's desktop-sized
# default caches waste pages during early browser startup.
CURL_CACHE_CFLAGS := \
	-DCURL_SOCKET_HASH_TABLE_SIZE=53 \
	-DCURL_CONNECTION_HASH_SIZE=17 \
	-DCURL_DNS_HASH_SIZE=17 \
	-DCURL_TLS_SESSION_SIZE=8

NS_ARFLAGS := rcs
NS_LDFLAGS := -L$(PREFIX)/lib

NETSURF_LIBS := libparserutils libwapcaplet libnsbmp libnsgif libnsutils libcss libhubbub libdom libnsfb

.PHONY: all root buildsystem base_libs mbedtls curl sdl12 vendor_libs licenses clean clean_root

all: vendor_libs

root: $(NETSURF_ROOT_STAMP)

$(NETSURF_ROOT_STAMP): buildsystem base_libs mbedtls curl sdl12 $(LIBM) licenses
	@mkdir -p $(PREFIX)
	touch $@

buildsystem:
	$(MAKE) -C $(NSSHARED) install PREFIX=$(PREFIX)

base_libs:
	$(MAKE) -C $(VENDOR) ARCH=$(ARCH) TOP_BUILD_DIR=$(TOP_BUILD_DIR) $(TOP_BUILD_DIR)/Vendor/Library/libz.a $(TOP_BUILD_DIR)/Vendor/Library/libpng.a $(TOP_BUILD_DIR)/Vendor/Library/libjpeg.a
	@mkdir -p $(PREFIX)/include $(PREFIX)/lib $(PREFIX)/lib/pkgconfig
	cp -R $(ZLIB_DIR)/zconf.h $(ZLIB_DIR)/zlib.h $(PREFIX)/include/
	cp -R $(LIBPNG_DIR)/png.h $(LIBPNG_DIR)/pngconf.h $(TOP_BUILD_DIR)/Vendor/Library/libpng/include/pnglibconf.h $(PREFIX)/include/
	cp -R $(LIBJPEG_DIR)/src/jpeglib.h $(LIBJPEG_DIR)/src/jmorecfg.h $(LIBJPEG_DIR)/src/jerror.h $(PREFIX)/include/
	cp -R $(TOP_BUILD_DIR)/Vendor/Library/libjpeg/include/jconfig.h $(TOP_BUILD_DIR)/Vendor/Library/libjpeg/include/jconfigint.h $(TOP_BUILD_DIR)/Vendor/Library/libjpeg/include/jversion.h $(PREFIX)/include/
	install -C -m 644 $(TOP_BUILD_DIR)/Vendor/Library/libz.a $(PREFIX)/lib/libz.a
	install -C -m 644 $(TOP_BUILD_DIR)/Vendor/Library/libpng.a $(PREFIX)/lib/libpng.a
	install -C -m 644 $(TOP_BUILD_DIR)/Vendor/Library/libjpeg.a $(PREFIX)/lib/libjpeg.a
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@REPO_ROOT@|$(REPO_ROOT)|g' \
		-e 's|@ARCH@|$(ARCH)|g' \
		pkgconfig/zlib.pc.in > $(ZLIB_PKGCONFIG)
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@REPO_ROOT@|$(REPO_ROOT)|g' \
		-e 's|@ARCH@|$(ARCH)|g' \
		pkgconfig/libpng.pc.in > $(LIBPNG_PKGCONFIG)
	sed -e 's|@PREFIX@|$(PREFIX)|g' \
		-e 's|@REPO_ROOT@|$(REPO_ROOT)|g' \
		-e 's|@ARCH@|$(ARCH)|g' \
		pkgconfig/libjpeg.pc.in > $(LIBJPEG_PKGCONFIG)

mbedtls:
	cmake -S $(MBEDTLS_DIR) -B $(MBEDTLS_BUILD_DIR) \
		-DCMAKE_SYSTEM_NAME=Generic \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_AR=$(AR) \
		-DCMAKE_RANLIB=$(RANLIB) \
		-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) \
		-DCMAKE_C_FLAGS="-ffreestanding -fno-pic -fno-builtin $(ARCH_CFLAGS) -nostdlib -O2 -I$(REPO_ROOT)/libc/I_libc/include" \
		-DENABLE_PROGRAMS=OFF \
		-DENABLE_TESTING=OFF \
		-DUSE_SHARED_MBEDTLS_LIBRARY=OFF \
		-DUSE_STATIC_MBEDTLS_LIBRARY=ON \
		-DMBEDTLS_USER_CONFIG_FILE="$(REPO_ROOT)/Userland/Service/NetSurf/MbedTlsShim/mbedtls_user_config.h"
	cmake --build $(MBEDTLS_BUILD_DIR)
	cmake --install $(MBEDTLS_BUILD_DIR)

curl: mbedtls
	cmake -S $(CURL_DIR) -B $(CURL_BUILD_DIR) \
		-C cmake/curl-implusos-cache.cmake \
		-DCMAKE_SYSTEM_NAME=Generic \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_AR=$(AR) \
		-DCMAKE_RANLIB=$(RANLIB) \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX) \
		-DCMAKE_PREFIX_PATH=$(PREFIX) \
		-DCMAKE_FIND_ROOT_PATH=$(PREFIX) \
		-DZLIB_INCLUDE_DIR=$(PREFIX)/include \
		-DZLIB_LIBRARY=$(PREFIX)/lib/libz.a \
		-DCMAKE_C_FLAGS="-ffreestanding -fno-pic -fno-builtin $(ARCH_CFLAGS) -nostdlib -O2 -I$(REPO_ROOT)/libc/I_libc/include -I$(PREFIX)/include $(CURL_CACHE_CFLAGS)" \
		-DCMAKE_EXE_LINKER_FLAGS="-nostdlib -L$(PREFIX)/lib" \
		-DBUILD_CURL_EXE=OFF \
		-DBUILD_EXAMPLES=OFF \
		-DBUILD_LIBCURL_DOCS=OFF \
		-DBUILD_MISC_DOCS=OFF \
		-DBUILD_SHARED_LIBS=OFF \
		-DBUILD_STATIC_LIBS=ON \
		-DCURL_DISABLE_SOCKETPAIR=ON \
		-DCURL_DISABLE_INSTALL=OFF \
		-DENABLE_THREADED_RESOLVER=OFF \
		-DCURL_USE_MBEDTLS=ON \
		-DCURL_USE_OPENSSL=OFF \
		-DCURL_USE_LIBPSL=OFF \
		-DCURL_USE_LIBSSH2=OFF \
		-DCURL_ZLIB=ON \
		-DCURL_DISABLE_FTP=ON \
		-DCURL_DISABLE_FILE=ON \
		-DCURL_DISABLE_LDAP=ON \
		-DCURL_DISABLE_LDAPS=ON \
		-DCURL_DISABLE_RTSP=ON \
		-DCURL_DISABLE_DICT=ON \
		-DCURL_DISABLE_TELNET=ON \
		-DCURL_DISABLE_TFTP=ON \
		-DCURL_DISABLE_POP3=ON \
		-DCURL_DISABLE_IMAP=ON \
		-DCURL_DISABLE_SMB=ON \
		-DCURL_DISABLE_SMTP=ON \
		-DCURL_DISABLE_GOPHER=ON \
		-DCURL_DISABLE_MQTT=ON
	cmake --build $(CURL_BUILD_DIR)
	cmake --install $(CURL_BUILD_DIR)

sdl12:
	$(MAKE) -C $(SDL12_DIR) all install ARCH=$(ARCH) PREFIX=$(PREFIX) TOP_BUILD_DIR=$(TOP_BUILD_DIR)

$(LIBM_OBJ): $(REPO_ROOT)/libc/I_libc/src/math.c
	@mkdir -p $(dir $@)
	$(CC) -ffreestanding -fno-pic -fno-builtin $(ARCH_CFLAGS) -nostdlib -O2 \
		-I$(REPO_ROOT)/libc/I_libc/include \
		-c $< -o $@

$(LIBM): $(LIBM_OBJ)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^

licenses:
	@mkdir -p $(LICENSE_DIR)/zlib $(LICENSE_DIR)/libpng $(LICENSE_DIR)/libjpeg $(LICENSE_DIR)/mbedtls $(LICENSE_DIR)/curl
	install -C -m 644 $(ZLIB_DIR)/LICENSE $(LICENSE_DIR)/zlib/LICENSE
	install -C -m 644 $(LIBPNG_DIR)/LICENSE.md $(LICENSE_DIR)/libpng/LICENSE.md
	install -C -m 644 $(LIBJPEG_DIR)/LICENSE.md $(LICENSE_DIR)/libjpeg/LICENSE.md
	install -C -m 644 $(MBEDTLS_DIR)/LICENSE $(LICENSE_DIR)/mbedtls/LICENSE
	install -C -m 644 $(CURL_DIR)/COPYING $(LICENSE_DIR)/curl/COPYING

# libdom needs expat binding disabled
libdom_EXTRA := WITH_EXPAT_BINDING=no

define BUILD_NS_LIB
$(1): root
	+CFLAGS="$(NS_CFLAGS)" $$(MAKE) -C $(VENDOR)/$(1) \
		CC="$(CC)" \
		AR="$(AR)" \
		BUILD_CC="$(BUILD_CC)" \
		PREFIX="$(PREFIX)" \
		NSSHARED="$(NSSHARED)" \
		ARFLAGS="$(NS_ARFLAGS)" \
		LDFLAGS="$(NS_LDFLAGS)" \
		PKGCONFIG="PKG_CONFIG_PATH=$(PREFIX)/lib/pkgconfig pkg-config" \
		HOST=x86_64-elf \
		BUILD=x86_64-elf \
		VARIANT=release \
		$$($(1)_EXTRA) \
		all install
endef

$(foreach lib,$(NETSURF_LIBS),$(eval $(call BUILD_NS_LIB,$(lib))))

vendor_libs: root $(NETSURF_LIBS)
	# Globalize static surface registration symbols in libnsfb
	# so that netsurf_start.c can call sdl_register_surface() and
	# ram_register_surface() (they are static due to NSFB_SURFACE_DEF macro).
	$(OBJCOPY) \
		--globalize-symbol=sdl_register_surface \
		--globalize-symbol=ram_register_surface \
		$(PREFIX)/lib/libnsfb.a 2>/dev/null || true

clean:
	for lib in $(NETSURF_LIBS); do \
		$(MAKE) -C $(VENDOR)/$$lib clean 2>/dev/null || true; \
	done
	rm -rf $(MBEDTLS_BUILD_DIR) $(CURL_BUILD_DIR) $(LIBM_BUILD_DIR)

clean_root:
	rm -rf $(PREFIX)
