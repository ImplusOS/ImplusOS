#!/usr/bin/env bash
# fetch.sh — packages.lock の .deb を cache/ へ取得し sha256 照合。
#   --verify-only : 取得せず既存 cache と（あれば）STAGE の閉包充足のみ検査。
#
# 入力(env): CACHE_DIR LOCKFILE [CLOSURE] [STAGE_DIR]
set -euo pipefail
: "${CACHE_DIR:?}" "${LOCKFILE:?}"

VERIFY_ONLY=0
[ "${1:-}" = "--verify-only" ] && VERIFY_ONLY=1

log(){ printf '[fetch] %s\n' "$*" >&2; }
die(){ printf '[fetch] ERROR: %s\n' "$*" >&2; exit 1; }

[ -r "$LOCKFILE" ] || die "no lockfile: $LOCKFILE (run 'make resolve' first)"
mkdir -p "$CACHE_DIR"

rc=0 n=0
while IFS=$'\t' read -r pkg ver url sha; do
	case "$pkg" in ''|\#*) continue ;; esac
	n=$((n+1))
	out="$CACHE_DIR/${url##*/}"
	if [ "$VERIFY_ONLY" -eq 0 ] && [ ! -s "$out" ]; then
		log "GET $pkg $ver"
		curl -fsSL --retry 3 --retry-delay 2 -o "$out.part" "$url" || { log "download failed: $url"; rc=1; continue; }
		mv "$out.part" "$out"
	fi
	[ -s "$out" ] || { log "MISSING: $out"; rc=1; continue; }
	if [ -n "$sha" ]; then
		got="$(sha256sum "$out" | cut -d' ' -f1)"
		if [ "$got" != "$sha" ]; then
			log "SHA MISMATCH $pkg"
			log "  expected $sha"
			log "  got      $got"
			rc=1
		fi
	else
		log "no sha in lock for $pkg (skipping check)"
	fi
done < "$LOCKFILE"
log "$n package(s) processed"

# 閉包充足チェック（closure.txt の全 soname が STAGE に存在するか）
if [ -n "${CLOSURE:-}" ] && [ -r "${CLOSURE:-}" ] && [ -n "${STAGE_DIR:-}" ] && [ -d "${STAGE_DIR:-}" ]; then
	miss=0
	while IFS=$'\t' read -r soname pkg sp; do
		case "$soname" in ''|\#*) continue ;; esac
		[ -e "$STAGE_DIR$sp" ] || { log "stage missing: $sp ($pkg)"; miss=$((miss+1)); }
	done < "$CLOSURE"
	[ "$miss" -eq 0 ] && log "closure satisfied in $STAGE_DIR" || { log "$miss soname(s) missing in stage"; rc=1; }
fi

exit $rc
