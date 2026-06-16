ifeq ($(strip $(ARCH)),)
$(error ARCH must be supplied by the top-level Makefile)
endif

ifeq ($(ARCH),arm64)
CROSS_COMPILE ?= aarch64-elf-
USERLAND_ARCH_CFLAGS ?= -mstrict-align -mno-outline-atomics
else ifeq ($(ARCH),x86_64)
CROSS_COMPILE ?= x86_64-elf-
USERLAND_ARCH_CFLAGS ?= -mcmodel=large -mno-red-zone
else
$(error Unsupported ARCH '$(ARCH)'. Use x86_64 or arm64.)
endif

CC := $(CROSS_COMPILE)gcc
CXX := $(CROSS_COMPILE)g++
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
ARCH_CFLAGS := $(USERLAND_ARCH_CFLAGS)

TOP_BUILD_DIR ?= ../../../../Build/$(ARCH)
COMMON_LIBS_DIR := $(TOP_BUILD_DIR)/Userland

SHARELIB_SRCS := $(shell find ../../../../ShareLib -name "*.c" 2>/dev/null)
COMMON_SHARELIB_OBJS := $(patsubst ../../../../ShareLib/%.c,$(TOP_BUILD_DIR)/ShareLib/%.o,$(SHARELIB_SRCS))

COMMON_OBJS := $(COMMON_LIBS_DIR)/Syscalls.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/string.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/stdlib.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/errno.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/posix.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/sys/syscalls.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/sys/$(ARCH)/hal_syscall.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/assert.o \
               $(COMMON_LIBS_DIR)/API/XMLParser.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/math.o \
               $(COMMON_LIBS_DIR)/libc/I_libc/src/stdio.o \
               $(COMMON_LIBS_DIR)/NetworkStack/DNS/DNS.o \
               $(COMMON_SHARELIB_OBJS)

COMMON_DEPS := $(COMMON_OBJS:.o=.d)
