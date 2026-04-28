PORTDIR  ?= $(dir $(abspath $(lastword $(MAKEFILE_LIST))))/..
TOPDIR   ?= $(abspath $(PORTDIR)/..)
SRCDIR   := $(PORTDIR)/src/wayland-protocols
INSTDIR  := $(PORTDIR)/sysroot

.PHONY: all install clean

all: install

install:
	@mkdir -p $(INSTDIR)/share/wayland-protocols
	@if [ -d "$(SRCDIR)" ]; then \
		cp -r $(SRCDIR)/stable $(INSTDIR)/share/wayland-protocols/ 2>/dev/null || true; \
		cp -r $(SRCDIR)/unstable $(INSTDIR)/share/wayland-protocols/ 2>/dev/null || true; \
		cp -r $(SRCDIR)/staging $(INSTDIR)/share/wayland-protocols/ 2>/dev/null || true; \
		echo "Wayland protocols installed"; \
	else \
		echo "wayland-protocols source not found, skipping"; \
	fi

clean:
	@true
