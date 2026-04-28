#!/bin/bash
set -e
PORTDIR="$(cd "$(dirname "$0")" && pwd)"
SRCDIR="$PORTDIR/src"
mkdir -p "$SRCDIR"

dl() { [ -d "$SRCDIR/$2" ] && return 0; echo "Fetching $2..."; git clone --depth=1 "$1" "$SRCDIR/$2" 2>/dev/null || wget -qO- "$1" | tar xz -C "$SRCDIR"; }

dl https://www.zlib.net/zlib-1.3.2.tar.gz                              zlib
dl https://github.com/glennrp/libpng.git                           libpng
dl https://gitlab.gnome.org/GNOME/glib.git                         glib
dl https://gitlab.freedesktop.org/freetype/freetype.git             freetype
dl https://gitlab.freedesktop.org/fontconfig/fontconfig.git         fontconfig
dl https://github.com/harfbuzz/harfbuzz.git                        harfbuzz
dl https://gitlab.freedesktop.org/cairo/cairo.git                  cairo
dl https://github.com/fribidi/fribidi.git                          fribidi
dl https://gitlab.gnome.org/GNOME/pango.git                        pango
dl https://gitlab.gnome.org/GNOME/gdk-pixbuf.git                   gdk-pixbuf
dl https://gitlab.gnome.org/GNOME/atk.git                          atk
dl https://github.com/anholt/libepoxy.git                          libepoxy
dl https://gitlab.freedesktop.org/dbus/dbus.git                    dbus
dl https://gitlab.gnome.org/GNOME/gtk.git                          gtk

echo "All GTK stack sources fetched to $SRCDIR"
