#!/usr/bin/env bash
# stage-x11data.sh — X クライアントが読む .so 以外のデータを STAGE_DIR へ配置する。
#                    stage.sh（.so 閉包）と stage-xorg.sh の後に走らせる。
#
#   - /usr/share/X11/locale … Xlib のロケールデータ（libx11-data）。無いと
#     XmbTextPropertyToTextList() などの i18n 経路が C ロケールでも失敗する。
#   - /usr/share/fonts/X11/misc/fonts.dir … xfonts-base は fonts.dir を postinst
#     で生成するため .deb に入っていない。無いと misc/ のフォントは XLFD 名で
#     引けず、Xorg 組み込みの "fixed" だけになる。ホストに mkfontdir があれば
#     生成する。fonts.alias は置かない: 置くと "fixed" の解決先が組み込みから
#     misc/ に変わり、xterm の見た目まで変わる。
#
# 入力(env): CACHE_DIR WORK_DIR STAGE_DIR
set -euo pipefail
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[x11data] %s\n' "$*" >&2; }
die(){ printf '[x11data] ERROR: %s\n' "$*" >&2; exit 1; }

W="$WORK_DIR/x11data"; rm -rf "$W"; mkdir -p "$W"

deb_of(){ ls "$CACHE_DIR"/"$1"_*.deb 2>/dev/null | head -n1; }

# ---- 1. Xlib ロケールデータ（libx11-data） ------------------------------------
# XmbTextPropertyToTextList() などは Xlib の /usr/share/X11/locale
# （locale.alias / locale.dir / <locale>/XLC_LOCALE）を引くので、無いと
# C ロケールでも失敗する（窓の名前が取れない等）。
LDEB="$(deb_of libx11-data)"
[ -n "$LDEB" ] || die "libx11-data .deb not in cache (run 'make fetch')"
L="$W/libx11-data"; mkdir -p "$L"
( cd "$L" && ar x "$LDEB" && \
  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
    tar -xf data.tar.gz 2>/dev/null || die "cannot extract $LDEB"; } )
[ -d "$L/usr/share/X11/locale" ] || die "usr/share/X11/locale missing in $LDEB"
mkdir -p "$STAGE_DIR/usr/share/X11"
rm -rf "$STAGE_DIR/usr/share/X11/locale"
# -L: 実体コピー（mtools は symlink を書けない）。
cp -RL "$L/usr/share/X11/locale" "$STAGE_DIR/usr/share/X11/locale"
[ -f "$L/usr/share/X11/XErrorDB" ] && install -m 0644 "$L/usr/share/X11/XErrorDB" "$STAGE_DIR/usr/share/X11/XErrorDB"
log "staged Xlib locale data ($(find "$STAGE_DIR/usr/share/X11/locale" -type f | wc -l) files)"

# ---- 2. misc フォントの fonts.dir ---------------------------------------------
MISC="$STAGE_DIR/usr/share/fonts/X11/misc"
if [ -d "$MISC" ]; then
	if command -v mkfontdir >/dev/null 2>&1; then
		mkfontdir "$MISC"
		log "generated misc/fonts.dir ($(head -n1 "$MISC/fonts.dir") fonts)"
	else
		log "WARN: mkfontdir not on host; only the built-in 'fixed' font resolves"
	fi
fi

log "done: $STAGE_DIR"
