#!/usr/bin/env bash
# stage-gtkdata.sh — GTK3 外来バイナリを起動可能にする「.so 以外の実行時データ」を
#                    STAGE_DIR へ配置する。stage.sh（.so 閉包）の後に実行する。
#
#   - gtk3-demo / gtk3-widget-factory / gtk3-demo-application 実行体を /usr/bin へ
#     （第三者バイナリ無改変。Doom/Chromium と同じく Debian の実バイナリをそのまま）
#   - GSettings スキーマ（org.gtk.Settings.* ほか）を集めて glib-compile-schemas で
#     gschemas.compiled を生成 … GLib/GIO は未コンパイルだと g_settings_new で abort
#   - DejaVu フォント + 最小 fonts.conf … これが無いと Pango がテキストを一切描けない
#   - gtk3-demo の同梱リソース（/usr/share/gtk-3.0 等）
#
#   - gdk-pixbuf のローダ群 + loaders.cache
#   - shared-mime-info の mime.cache … gdk-pixbuf は先頭バイトからの形式判定を
#     自前の署名比較ではなく GIO の g_content_type_guess() で行うので、これが
#     無いとローダも画像データも正しいのに "Unrecognized image file format" に
#     なり、GTK は最初に描くアイコンで g_assert に落ちる
#
#   - Adwaita のカーソル + アイコン … GDK はカーソルテーマが 1 つも読めないと
#     cursor_theme_name を NULL のままにし、後でそれを g_assert して落ちる
#
# 入力(env): CACHE_DIR WORK_DIR STAGE_DIR
set -euo pipefail
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[gtkdata] %s\n' "$*" >&2; }
die(){ printf '[gtkdata] ERROR: %s\n' "$*" >&2; exit 1; }

W="$WORK_DIR/gtkdata"; rm -rf "$W"; mkdir -p "$W"

deb_of(){ ls "$CACHE_DIR"/"$1"_*.deb 2>/dev/null | head -n1; }

extract(){ # tag deb
	local d="$W/$1"
	mkdir -p "$d"
	( cd "$d" && ar x "$2" && \
	  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
	    tar -xf data.tar.gz 2>/dev/null || die "cannot extract $2"; } )
	printf '%s' "$d"
}

# ---- 1. gtk3-demo 実行体 + 同梱リソース --------------------------------------
EXDEB="$(deb_of gtk-3-examples)"; [ -n "$EXDEB" ] || die "gtk-3-examples deb not in cache"
exdir="$(extract gtk-3-examples "$EXDEB")"
mkdir -p "$STAGE_DIR/usr/bin"
for b in gtk3-demo gtk3-widget-factory gtk3-demo-application gtk3-icon-browser; do
	[ -f "$exdir/usr/bin/$b" ] && install -m 0755 "$exdir/usr/bin/$b" "$STAGE_DIR/usr/bin/$b"
done
# gtk3-demo のデモソース等（/usr/share/gtk-3.0/... , /usr/share/gtk-3-examples/...）
if [ -d "$exdir/usr/share" ]; then
	mkdir -p "$STAGE_DIR/usr/share"
	cp -a "$exdir/usr/share/." "$STAGE_DIR/usr/share/"
fi
log "staged $(ls "$STAGE_DIR/usr/bin" | tr '\n' ' ')"

# ---- 2. GSettings スキーマ → gschemas.compiled ------------------------------
command -v glib-compile-schemas >/dev/null || die "glib-compile-schemas missing on host"
schemas="$STAGE_DIR/usr/share/glib-2.0/schemas"
mkdir -p "$schemas"
for p in libgtk-3-common gsettings-desktop-schemas libglib2.0-0t64 libglib2.0-0; do
	deb="$(deb_of "$p")" || true
	[ -n "${deb:-}" ] || continue
	d="$(extract "$p" "$deb")"
	find "$d" -path '*/glib-2.0/schemas/*.gschema.xml' -exec cp -f {} "$schemas/" \; 2>/dev/null || true
	find "$d" -path '*/glib-2.0/schemas/*.enums.xml'   -exec cp -f {} "$schemas/" \; 2>/dev/null || true
done
n=$(ls "$schemas"/*.xml 2>/dev/null | wc -l)
[ "$n" -gt 0 ] || die "no gschema xml collected"
glib-compile-schemas "$schemas"
[ -f "$schemas/gschemas.compiled" ] || die "gschemas.compiled not produced"
log "compiled $n schema file(s) -> gschemas.compiled"

# ---- 3. フォント + fontconfig ----------------------------------------------
FDEB="$(deb_of fonts-dejavu-core)"
if [ -n "$FDEB" ]; then
	fdir="$(extract fonts-dejavu-core "$FDEB")"
	dst="$STAGE_DIR/usr/share/fonts/truetype/dejavu"
	mkdir -p "$dst"
	find "$fdir" -name '*.ttf' -exec install -m 0644 {} "$dst/" \;
	log "staged $(ls "$dst" | wc -l) DejaVu ttf"
else
	log "WARN: fonts-dejavu-core not in cache; Pango will have no font"
fi
mkdir -p "$STAGE_DIR/etc/fonts"
cat > "$STAGE_DIR/etc/fonts/fonts.conf" <<'XML'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <cachedir>/tmp/fontconfig</cachedir>
  <match target="pattern"><test name="family"><string>sans-serif</string></test>
    <edit name="family" mode="prepend" binding="strong"><string>DejaVu Sans</string></edit></match>
  <match target="pattern"><test name="family"><string>monospace</string></test>
    <edit name="family" mode="prepend" binding="strong"><string>DejaVu Sans Mono</string></edit></match>
  <match target="pattern"><test name="family"><string>serif</string></test>
    <edit name="family" mode="prepend" binding="strong"><string>DejaVu Serif</string></edit></match>
  <match target="pattern"><edit name="family" mode="append" binding="weak"><string>DejaVu Sans</string></edit></match>
</fontconfig>
XML
log "wrote /etc/fonts/fonts.conf"

# ---- 4. gdk-pixbuf ローダ + loaders.cache ----------------------------------
# ローダは dlopen されるだけで DT_NEEDED に現れないので、.so 閉包を辿る
# stage.sh では拾えない。実行体 gdk-pixbuf-query-loaders は
# libgdk-pixbuf-2.0-0 の deb に同梱されている（別パッケージは要らない）。
# 生成したキャッシュはローダの絶対パスを持つので、STAGE_DIR 接頭辞を落として
# ゲスト上のパスに直す。
PBDEB="$(deb_of libgdk-pixbuf-2.0-0)"
if [ -n "$PBDEB" ]; then
	pbdir="$(extract libgdk-pixbuf-2.0-0 "$PBDEB")"
	pbrel="usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0"
	if [ -d "$pbdir/$pbrel/2.10.0/loaders" ]; then
		mkdir -p "$STAGE_DIR/$pbrel/2.10.0/loaders"
		cp -a "$pbdir/$pbrel/2.10.0/loaders/." \
		      "$STAGE_DIR/$pbrel/2.10.0/loaders/"
		q="$pbdir/$pbrel/gdk-pixbuf-query-loaders"
		ld="$STAGE_DIR/lib64/ld-linux-x86-64.so.2"
		cache="$STAGE_DIR/$pbrel/2.10.0/loaders.cache"
		if [ -x "$q" ] && [ -x "$ld" ]; then
			# Run the vendored tool against the vendored loaders, using the
			# vendored ld.so -- the build host's own gdk-pixbuf is a
			# different version and would write a cache this one rejects.
			GDK_PIXBUF_MODULEDIR="$STAGE_DIR/$pbrel/2.10.0/loaders" \
			"$ld" --library-path "$STAGE_DIR/usr/lib/x86_64-linux-gnu" \
			      "$q" > "$cache" 2>/dev/null || true
			sed -i "s|$STAGE_DIR||g" "$cache"
			n=$(grep -c '^"/usr' "$cache" 2>/dev/null || echo 0)
			[ "$n" -gt 0 ] || die "loaders.cache has no loaders"
			log "gdk-pixbuf: $n loader(s) -> loaders.cache"
		else
			log "WARN: cannot run gdk-pixbuf-query-loaders; no loaders.cache"
		fi
	fi
else
	log "WARN: libgdk-pixbuf-2.0-0 not in cache; no image loaders"
fi

# ---- 5. shared-mime-info（mime.cache） --------------------------------------
# deb は XML しか持たない（Debian は postinst で作る）ので、同梱の
# update-mime-database を同梱の ld.so 越しに走らせてコンパイルする。
# ホストにも同名のツールはあるが、バージョンの違うキャッシュを混ぜたくない。
SMIDEB="$(deb_of shared-mime-info)"
if [ -n "$SMIDEB" ]; then
	smidir="$(extract shared-mime-info "$SMIDEB")"
	if [ -d "$smidir/usr/share/mime/packages" ]; then
		mkdir -p "$STAGE_DIR/usr/share/mime/packages"
		cp -a "$smidir/usr/share/mime/packages/." \
		      "$STAGE_DIR/usr/share/mime/packages/"
		umd="$smidir/usr/bin/update-mime-database"
		ld="$STAGE_DIR/lib64/ld-linux-x86-64.so.2"
		if [ -x "$umd" ] && [ -x "$ld" ]; then
			"$ld" --library-path "$STAGE_DIR/usr/lib/x86_64-linux-gnu" \
			      "$umd" "$STAGE_DIR/usr/share/mime" >/dev/null 2>&1 || true
		fi
		[ -s "$STAGE_DIR/usr/share/mime/mime.cache" ] || \
			die "mime.cache not produced"
		log "mime.cache $(stat -c %s "$STAGE_DIR/usr/share/mime/mime.cache") bytes"
	fi
else
	log "WARN: shared-mime-info not in cache; gdk-pixbuf cannot sniff formats"
fi

# ---- 6. Adwaita カーソル / アイコンテーマ -----------------------------------
ADWDEB="$(deb_of adwaita-icon-theme)"
if [ -n "$ADWDEB" ]; then
	adwdir="$(extract adwaita-icon-theme "$ADWDEB")"
	if [ -d "$adwdir/usr/share/icons" ]; then
		mkdir -p "$STAGE_DIR/usr/share/icons"
		cp -a "$adwdir/usr/share/icons/." "$STAGE_DIR/usr/share/icons/"
		c=$(ls "$STAGE_DIR/usr/share/icons/Adwaita/cursors" 2>/dev/null | wc -l)
		[ "$c" -gt 0 ] || die "no Adwaita cursors staged"
		log "Adwaita: $c cursor(s)"
	fi
else
	log "WARN: adwaita-icon-theme not in cache; GDK will abort on the cursor theme"
fi

log "done: $STAGE_DIR"
