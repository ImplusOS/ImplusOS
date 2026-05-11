PLATFORM_SRCS := $(shell find Platform -type f -name '*.c' -print | sort)
OBJS += $(patsubst %.c,$(KERNEL_BUILD)/%.o,$(PLATFORM_SRCS))
