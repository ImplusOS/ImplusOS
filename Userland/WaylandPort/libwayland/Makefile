PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/wayland
BUILDDIR := $(PORTDIR)/build/libwayland
INSTDIR  := $(PORTDIR)/sysroot
CC       := x86_64-elf-gcc
AR       := x86_64-elf-ar

CFLAGS := -ffreestanding -fno-stack-protector -fno-pic -fno-builtin \
    -mno-red-zone -nostdlib -O2 -w \
    -I$(SRCDIR)/src -I$(SRCDIR)/protocol \
    -I$(INSTDIR)/include \
    -I$(TOPDIR)/musl/include -I$(TOPDIR)/musl/arch/implus \
    -I$(TOPDIR)/musl/arch/generic -I$(TOPDIR)/libc/include \
    -I$(PORTDIR)/adapters/include -I$(PORTDIR)/libwayland \
    -DHAVE_ACCEPT4=0 -DHAVE_MKOSTEMP=0 -DHAVE_POSIX_FALLOCATE=0 \
    -DHAVE_MEMFD_CREATE=0 -DHAVE_STRNDUP=1

SERVER_SRCS := wayland-server.c wayland-shm.c event-loop.c
CLIENT_SRCS := wayland-client.c
COMMON_SRCS := wayland-util.c wayland-os.c connection.c wayland-private.h

SERVER_OBJS := $(addprefix $(BUILDDIR)/server_, $(notdir $(SERVER_SRCS:.c=.o)))
CLIENT_OBJS := $(addprefix $(BUILDDIR)/client_, $(notdir $(CLIENT_SRCS:.c=.o)))
COMMON_OBJS := $(addprefix $(BUILDDIR)/, $(filter %.o, $(COMMON_SRCS:.c=.o)))

ALL_SERVER_OBJS := $(SERVER_OBJS) $(COMMON_OBJS)
ALL_CLIENT_OBJS := $(CLIENT_OBJS) $(COMMON_OBJS)

.PHONY: all install clean

all: $(BUILDDIR)/libwayland-server.a $(BUILDDIR)/libwayland-client.a install

$(BUILDDIR)/libwayland-server.a: $(ALL_SERVER_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/libwayland-client.a: $(ALL_CLIENT_OBJS)
	$(AR) rcs $@ $^

$(BUILDDIR)/server_%.o: $(SRCDIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/client_%.o: $(SRCDIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/%.o: $(SRCDIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BUILDDIR)/libwayland-server.a $(BUILDDIR)/libwayland-client.a
	@mkdir -p $(INSTDIR)/lib $(INSTDIR)/include
	cp $(BUILDDIR)/libwayland-server.a $(INSTDIR)/lib/
	cp $(BUILDDIR)/libwayland-client.a $(INSTDIR)/lib/
	cp $(SRCDIR)/src/wayland-server.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/src/wayland-client.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/src/wayland-server-core.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/src/wayland-client-core.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/src/wayland-util.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/src/wayland-version.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/protocol/wayland-server-protocol.h $(INSTDIR)/include/ 2>/dev/null || true
	cp $(SRCDIR)/protocol/wayland-client-protocol.h $(INSTDIR)/include/ 2>/dev/null || true

clean:
	rm -rf $(BUILDDIR)
