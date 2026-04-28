#!/bin/bash
set -e
PORTDIR="$(cd "$(dirname "$0")" && pwd)"
SRCDIR="$PORTDIR/src"
mkdir -p "$SRCDIR"

dl() { [ -d "$SRCDIR/$2" ] && return 0; echo "Fetching $2..."; git clone --depth=1 "$1" "$SRCDIR/$2" 2>/dev/null || wget -qO- "$1" | tar xz -C "$SRCDIR"; }

dl https://gitlab.freedesktop.org/pixman/pixman.git pixman
dl https://gitlab.freedesktop.org/mesa/drm.git libdrm
dl https://github.com/xkbcommon/libxkbcommon.git libxkbcommon
dl https://gitlab.freedesktop.org/libevdev/libevdev.git libevdev
dl https://gitlab.freedesktop.org/libinput/libinput.git libinput
dl https://github.com/libffi/libffi.git libffi
dl https://gitlab.freedesktop.org/wayland/wayland.git wayland
dl https://gitlab.freedesktop.org/wayland/wayland-protocols.git wayland-protocols
dl https://zlib.net/zlib-1.3.1.tar.gz zlib
dl https://github.com/libexpat/libexpat.git libexpat
dl https://gitlab.freedesktop.org/mesa/mesa.git mesa

echo "All sources fetched to $SRCDIR"
