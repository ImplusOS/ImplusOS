DRIVER_CLIENT_SRCS := $(shell find Drivers -path Drivers/Server -prune -o -type f -name '*.c' -print | sort)
OBJS += $(patsubst %.c,$(KERNEL_BUILD)/%.o,$(DRIVER_CLIENT_SRCS))
