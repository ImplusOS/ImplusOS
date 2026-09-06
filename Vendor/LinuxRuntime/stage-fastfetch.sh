#!/usr/bin/env bash
# stage-fastfetch.sh — FastFetch 外来バイナリを起動可能にする「.so 以外の実行時
#                      データ」を STAGE_DIR へ配置する。stage.sh（.so 閉包）の後。
#
#   - /usr/bin/fastfetch 実行体（Debian trixie の実バイナリを無改変でそのまま）
#   - /usr/share/fastfetch/presets/*.jsonc（--load-config 用。無くても既定表示は動く）
#
# fastfetch の DT_NEEDED は libyyjson.so.0 / libm / libc の 3 本だけで、それらは
# stage.sh が閉包として配置済み。libpci / libvulkan / libwayland-client / libX11 /
# libGL / libdconf などは実行時 dlopen の任意依存なので、ImplusOS に無ければ
# 対応する項目が省かれるだけで起動そのものには影響しない。
#
# 入力(env): CACHE_DIR WORK_DIR STAGE_DIR
set -euo pipefail
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[fastfetch] %s\n' "$*" >&2; }
die(){ printf '[fastfetch] ERROR: %s\n' "$*" >&2; exit 1; }

W="$WORK_DIR/fastfetch"; rm -rf "$W"; mkdir -p "$W"

deb_of(){ ls "$CACHE_DIR"/"$1"_*.deb 2>/dev/null | head -n1; }

extract(){ # tag deb -> 展開ディレクトリ
	local d="$W/$1"
	mkdir -p "$d"
	( cd "$d" && ar x "$2" && \
	  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
	    tar -xf data.tar.gz 2>/dev/null || die "cannot extract $2"; } )
	printf '%s' "$d"
}

FFDEB="$(deb_of fastfetch)"; [ -n "$FFDEB" ] || die "fastfetch deb not in cache (run 'make fetch')"
ffdir="$(extract fastfetch "$FFDEB")"

[ -f "$ffdir/usr/bin/fastfetch" ] || die "usr/bin/fastfetch not in deb"
mkdir -p "$STAGE_DIR/usr/bin"
install -m 0755 "$ffdir/usr/bin/fastfetch" "$STAGE_DIR/usr/bin/fastfetch"
log "staged /usr/bin/fastfetch ($(stat -c%s "$STAGE_DIR/usr/bin/fastfetch") bytes, unmodified)"

if [ -d "$ffdir/usr/share/fastfetch" ]; then
	mkdir -p "$STAGE_DIR/usr/share/fastfetch"
	cp -a "$ffdir/usr/share/fastfetch/." "$STAGE_DIR/usr/share/fastfetch/"
	log "staged $(find "$STAGE_DIR/usr/share/fastfetch" -name '*.jsonc' | wc -l) preset(s)"
fi

log "done: $STAGE_DIR"
