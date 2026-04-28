PORTDIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

.PHONY: all clean zlib libpng glib freetype fontconfig harfbuzz cairo fribidi \
        pango gdk-pixbuf atk libepoxy dbus gtk3

all: zlib libpng glib freetype fontconfig harfbuzz fribidi cairo pango gdk-pixbuf atk libepoxy dbus gtk3
	@echo "=== GTKPort: All libraries built ==="

zlib:
	@echo "--- Building zlib ---"
	$(MAKE) -f $(PORTDIR)/zlib/Makefile PORTDIR=$(PORTDIR)

libpng: zlib
	@echo "--- Building libpng ---"
	$(MAKE) -f $(PORTDIR)/libpng/Makefile PORTDIR=$(PORTDIR)

glib:
	@echo "--- Building GLib ---"
	$(MAKE) -f $(PORTDIR)/glib/Makefile PORTDIR=$(PORTDIR)

freetype:
	@echo "--- Building FreeType ---"
	$(MAKE) -f $(PORTDIR)/freetype/Makefile PORTDIR=$(PORTDIR)

fontconfig: freetype
	@echo "--- Building fontconfig ---"
	$(MAKE) -f $(PORTDIR)/fontconfig/Makefile PORTDIR=$(PORTDIR)

harfbuzz: freetype
	@echo "--- Building HarfBuzz ---"
	$(MAKE) -f $(PORTDIR)/harfbuzz/Makefile PORTDIR=$(PORTDIR)

fribidi:
	@echo "--- Building fribidi ---"
	$(MAKE) -f $(PORTDIR)/fribidi/Makefile PORTDIR=$(PORTDIR)

cairo: freetype glib
	@echo "--- Building cairo ---"
	$(MAKE) -f $(PORTDIR)/cairo/Makefile PORTDIR=$(PORTDIR)

pango: glib freetype fontconfig harfbuzz fribidi cairo
	@echo "--- Building Pango ---"
	$(MAKE) -f $(PORTDIR)/pango/Makefile PORTDIR=$(PORTDIR)

gdk-pixbuf: glib libpng
	@echo "--- Building gdk-pixbuf ---"
	$(MAKE) -f $(PORTDIR)/gdk-pixbuf/Makefile PORTDIR=$(PORTDIR)

atk: glib
	@echo "--- Building ATK ---"
	$(MAKE) -f $(PORTDIR)/atk/Makefile PORTDIR=$(PORTDIR)

libepoxy:
	@echo "--- Building libepoxy ---"
	$(MAKE) -f $(PORTDIR)/libepoxy/Makefile PORTDIR=$(PORTDIR)

dbus:
	@echo "--- Building D-Bus ---"
	$(MAKE) -f $(PORTDIR)/dbus/Makefile PORTDIR=$(PORTDIR)

gtk3: glib cairo pango gdk-pixbuf atk libepoxy dbus
	@echo "--- Building GTK3 ---"
	$(MAKE) -f $(PORTDIR)/gtk3/Makefile PORTDIR=$(PORTDIR)

clean:
	rm -rf $(PORTDIR)/build $(PORTDIR)/sysroot
