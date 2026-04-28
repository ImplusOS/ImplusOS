COMMON_LIBS_DIR := ../../../../Build/Userland

COMMON_OBJS := $(COMMON_LIBS_DIR)/Syscalls.o \
               $(COMMON_LIBS_DIR)/libc/src/string.o \
               $(COMMON_LIBS_DIR)/libc/src/stdlib.o \
               $(COMMON_LIBS_DIR)/libc/src/errno.o \
               $(COMMON_LIBS_DIR)/libc/src/posix.o \
               $(COMMON_LIBS_DIR)/libc/src/sys/syscalls.o \
               $(COMMON_LIBS_DIR)/libc/src/assert.o \
               $(COMMON_LIBS_DIR)/API/XMLParser.o \
               $(COMMON_LIBS_DIR)/libc/src/math.o \
               $(COMMON_LIBS_DIR)/libc/src/stdio.o \
               $(COMMON_LIBS_DIR)/NetworkStack/DNS/DNS.o
