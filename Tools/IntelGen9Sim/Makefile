# Host-side simulator test for the IntelGen9 display driver module.
#   make -C Tools/IntelGen9Sim test
# Builds the unmodified driver sources for the host with -DIG9_HOST_SIM and
# runs them against the GPU model in IG9_Sim.c.

ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
DRV_DIR  := $(ROOT_DIR)/Kernel/Source/Drivers/Display/IntelGen9
BUILD    := $(ROOT_DIR)/Build/Host/IntelGen9Sim

HOST_CC ?= gcc
CFLAGS  := -std=c11 -D_DEFAULT_SOURCE -O1 -g -Wall -Wextra -Wconversion -Wsign-conversion \
           -Wshadow -DIMPLUS_DRIVER_MODULE -DIG9_HOST_SIM \
           -I$(ROOT_DIR)/Kernel/Source -I$(ROOT_DIR)/Kernel/Source/include \
           -I$(ROOT_DIR)/Kernel/Source/Drivers -I$(DRV_DIR)

DRV_SRCS := $(wildcard $(DRV_DIR)/*.c)
SIM_SRCS := IG9_Sim.c

.PHONY: all test clean

all: $(BUILD)/ig9_sim

$(BUILD)/ig9_sim: $(SIM_SRCS) $(DRV_SRCS) $(wildcard $(DRV_DIR)/*.h)
	@mkdir -p $(BUILD)
	$(HOST_CC) $(CFLAGS) -Wno-conversion -Wno-sign-conversion $(SIM_SRCS) $(DRV_SRCS) -o $@

test: $(BUILD)/ig9_sim
	$(BUILD)/ig9_sim

clean:
	rm -rf $(BUILD)
