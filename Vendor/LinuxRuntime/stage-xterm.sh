#!/usr/bin/env bash
# stage-xterm.sh — xterm 外来バイナリを起動可能にする「.so 以外の実行時データ」を
#                  STAGE_DIR へ配置する。stage.sh（.so 閉包）の後に走らせる。
#
#   - /usr/bin/xterm 実行体（Debian trixie の実バイナリを無改変でそのまま）
#   - /etc/X11/app-defaults/XTerm{,-color} … Xt のリソース既定値。無くても
#     組み込み既定で起動はするが、色や keymap がここに入っている。
#   - /usr/share/terminfo/x/xterm* + /usr/share/terminfo/l/linux（ncurses-base）
#     … xterm 自身は要らない。TERM=xterm を読むのは「中で走るアプリ」の方で、
#     terminfo が無いと curses アプリが "Error opening terminal" で死ぬ。
#
# 既定フォントについて: xterm は -fa を渡さない限り X コアフォント（fixed）で
# 描く。fixed は Xorg 用に stage-xorg.sh が既に /usr/share/fonts/X11/misc へ
# 置いているので、フォント側の追加作業は無い。-fa を使う場合だけ Xft →
# fontconfig → DejaVu の経路に入る（stage-gtkdata.sh が配置済み）。
#
# 入力(env): CACHE_DIR WORK_DIR STAGE_DIR
set -euo pipefail
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[xterm] %s\n' "$*" >&2; }
die(){ printf '[xterm] ERROR: %s\n' "$*" >&2; exit 1; }

W="$WORK_DIR/xterm"; rm -rf "$W"; mkdir -p "$W"

deb_of(){ ls "$CACHE_DIR"/"$1"_*.deb 2>/dev/null | head -n1; }

extract(){ # tag deb -> 展開ディレクトリ
	local d="$W/$1"
	mkdir -p "$d"
	( cd "$d" && ar x "$2" && \
	  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
	    tar -xf data.tar.gz 2>/dev/null || die "cannot extract $2"; } )
	printf '%s' "$d"
}

# src の rel 以下を dst へ実体コピー（symlink は readlink -f で解決）。
copy_tree_real(){ # srcdir dstdir
	local src="$1" dst="$2" f rel t
	[ -d "$src" ] || return 0
	while IFS= read -r f; do
		rel="${f#"$src"/}"
		mkdir -p "$dst/$(dirname "$rel")"
		if [ -L "$f" ]; then
			t="$(readlink -f "$f" 2>/dev/null || true)"
			[ -n "$t" ] && [ -f "$t" ] && install -m 0644 "$t" "$dst/$rel"
		elif [ -f "$f" ]; then
			install -m 0644 "$f" "$dst/$rel"
		fi
	done < <(find "$src" -mindepth 1 \( -type f -o -type l \))
}

# ---- 1. xterm 実行体 --------------------------------------------------------
XDEB="$(deb_of xterm)"
[ -n "$XDEB" ] || die "xterm .deb not in cache (run 'make fetch')"
xdir="$(extract xterm "$XDEB")"
[ -f "$xdir/usr/bin/xterm" ] || die "usr/bin/xterm missing in the .deb"
mkdir -p "$STAGE_DIR/usr/bin"
install -m 0755 "$xdir/usr/bin/xterm" "$STAGE_DIR/usr/bin/xterm"
log "staged /usr/bin/xterm ($(stat -c%s "$STAGE_DIR/usr/bin/xterm") bytes)"

# ---- 2. Xt app-defaults -----------------------------------------------------
# Debian ships them in /etc/X11; upstream Xt's compiled-in XFILESEARCHPATH
# looks in /usr/share/X11. Put them in both so neither lookup misses.
mkdir -p "$STAGE_DIR/etc/X11/app-defaults" "$STAGE_DIR/usr/share/X11/app-defaults"
for res in XTerm XTerm-color; do
	if [ -f "$xdir/etc/X11/app-defaults/$res" ]; then
		install -m 0644 "$xdir/etc/X11/app-defaults/$res" \
			"$STAGE_DIR/etc/X11/app-defaults/$res"
		install -m 0644 "$xdir/etc/X11/app-defaults/$res" \
			"$STAGE_DIR/usr/share/X11/app-defaults/$res"
	fi
done
log "staged app-defaults: $(ls "$STAGE_DIR/etc/X11/app-defaults" | tr '\n' ' ')"

# ---- 3. terminfo ------------------------------------------------------------
NDEB="$(deb_of ncurses-base)"
if [ -n "$NDEB" ]; then
	ndir="$(extract ncurses-base "$NDEB")"
	src="$ndir/usr/share/terminfo"
	dst="$STAGE_DIR/usr/share/terminfo"
	n=0
	for entry in x/xterm x/xterm-color x/xterm-256color x/xterm-debian \
	             l/linux v/vt100 v/vt102 v/vt220 d/dumb a/ansi; do
		if [ -f "$src/$entry" ]; then
			mkdir -p "$dst/$(dirname "$entry")"
			install -m 0644 "$src/$entry" "$dst/$entry"
			n=$((n+1))
		fi
	done
	log "staged $n terminfo entries"
else
	log "WARN: ncurses-base not in cache; curses apps inside xterm will fail"
fi

log "done: $STAGE_DIR"
