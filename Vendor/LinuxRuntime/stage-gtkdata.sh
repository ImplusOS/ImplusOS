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
# 生成しない（既知の TODO、TODO_GTK3_Wayland_LinuxABI.md §G4）:
#   - gdk-pixbuf loaders.cache（host に gdk-pixbuf-query-loaders が無い。
#     ラスタ画像ローダ不在。ウィンドウは出るがアイコン/画像は出ない）
#   - Adwaita アイコンテーマ（巨大。hicolor フォールバックのみ）
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

log "done: $STAGE_DIR"
