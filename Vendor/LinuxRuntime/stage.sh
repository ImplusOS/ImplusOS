#!/usr/bin/env bash
# stage.sh — cache/*.deb を展開し STAGE_DIR へ配置する。
#   - 第三者バイナリは無改変（そのままコピー）。
#   - symlink は実体に解決してコピー（mtools が symlink を扱えないため）。
#   - ld-linux-x86-64.so.2 は /lib64 へ、その他 .so は /usr/lib/x86_64-linux-gnu へ。
#   - 各 .deb の copyright を LICENSE_DIR へ収集。
#   - 最後に closure.txt の全 soname が揃ったか自己検査。
#
# 入力(env): CACHE_DIR WORK_DIR LOCKFILE CLOSURE LICENSE_DIR STAGE_DIR
set -euo pipefail
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${LOCKFILE:?}" "${CLOSURE:?}" "${LICENSE_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[stage] %s\n' "$*" >&2; }
die(){ printf '[stage] ERROR: %s\n' "$*" >&2; exit 1; }

[ -r "$LOCKFILE" ] || die "no lockfile (run 'make resolve')"
[ -r "$CLOSURE" ]  || die "no closure.txt (run 'make resolve')"

EX="$WORK_DIR/stage-extract"; rm -rf "$EX"; mkdir -p "$EX"
mkdir -p "$STAGE_DIR/lib64" "$STAGE_DIR/usr/lib/x86_64-linux-gnu" \
         "$STAGE_DIR/usr/lib/locale" "$LICENSE_DIR"

# ---- 1. 全 .deb を展開 -----------------------------------------------------
while IFS=$'\t' read -r pkg ver url sha; do
	case "$pkg" in ''|\#*) continue ;; esac
	deb="$CACHE_DIR/${url##*/}"
	[ -s "$deb" ] || die "missing .deb for $pkg (run 'make fetch')"
	d="$EX/$pkg"; mkdir -p "$d"
	( cd "$d" && ar x "$deb" && \
	  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
	    tar -xf data.tar.gz 2>/dev/null || die "cannot extract data.tar for $pkg"; } )
	# copyright 収集
	cp_src="$(find "$d/usr/share/doc" -maxdepth 2 -name copyright 2>/dev/null | head -n1 || true)"
	[ -n "$cp_src" ] && cp -f "$cp_src" "$LICENSE_DIR/${pkg}.copyright" || log "no copyright in $pkg"
done < "$LOCKFILE"

# ---- 2. closure.txt に従って配置（実体解決コピー） -----------------------
# 展開ツリーから soname を探す。複数版があれば最初に見つかったものを使う。
find_real(){ # soname -> 実体ファイルパス
	local want="$1" f
	while IFS= read -r f; do
		# シンボリックリンクなら実体へ
		if [ -L "$f" ]; then
			local t; t="$(readlink -f "$f" 2>/dev/null || true)"
			[ -n "$t" ] && [ -e "$t" ] && { printf '%s' "$t"; return 0; }
		elif [ -f "$f" ]; then
			printf '%s' "$f"; return 0
		fi
	done < <(find "$EX" -name "$want" 2>/dev/null)
	return 1
}

placed=0 missing=0
while IFS=$'\t' read -r soname pkg sp; do
	case "$soname" in ''|\#*) continue ;; esac
	real="$(find_real "$soname" || true)"
	if [ -z "$real" ]; then
		log "NOT FOUND in extracted debs: $soname (pkg $pkg)"
		missing=$((missing+1)); continue
	fi
	dst="$STAGE_DIR$sp"
	mkdir -p "$(dirname "$dst")"
	install -m 0755 "$real" "$dst"
	placed=$((placed+1))
	# 実体が持つ SONAME での別名も張っておく（ld.so の検索名対策）。
	realsoname="$(readelf -d "$real" 2>/dev/null | awk -F'[][]' '/\(SONAME\)/{print $2}')"
	if [ -n "$realsoname" ] && [ "$realsoname" != "$soname" ]; then
		alt="$(dirname "$dst")/$realsoname"
		[ -e "$alt" ] || install -m 0755 "$real" "$alt"
	fi
done < "$CLOSURE"

# ---- 3. ld-linux も明示配置（closure に無い場合の保険） -----------------
if [ ! -e "$STAGE_DIR/lib64/ld-linux-x86-64.so.2" ]; then
	real="$(find_real ld-linux-x86-64.so.2 || true)"
	[ -n "$real" ] && install -m 0755 "$real" "$STAGE_DIR/lib64/ld-linux-x86-64.so.2" \
	              || log "ld-linux-x86-64.so.2 not found (libc6 deb missing?)"
fi

# ---- 4. 自己検査 --------------------------------------------------------------
log "placed=$placed missing=$missing"
[ -e "$STAGE_DIR/lib64/ld-linux-x86-64.so.2" ] || { log "FATAL: no dynamic linker staged"; missing=$((missing+1)); }
[ "$missing" -eq 0 ] || die "$missing soname(s) could not be staged"
log "stage complete: $STAGE_DIR"
