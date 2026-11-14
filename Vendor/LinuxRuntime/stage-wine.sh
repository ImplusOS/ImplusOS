#!/usr/bin/env bash
# stage-wine.sh — stage the non-DT_NEEDED part of the x86_64 Wine runtime.
#
# Wine is different from the other foreign programs in this directory: most
# of its Windows API implementation is a tree of PE DLLs loaded by wine64 at
# run time. closure.txt therefore cannot discover or place it. The package
# extraction done by stage.sh is reused here, and only the runtime paths are
# copied into the image.
set -euo pipefail
: "${WORK_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[wine] %s\n' "$*" >&2; }
die(){ printf '[wine] ERROR: %s\n' "$*" >&2; exit 1; }

EX="$WORK_DIR/stage-extract"
W="$WORK_DIR/wine"
UNION="$W/union"
rm -rf "$W"
mkdir -p "$UNION"

# The package names are intentionally explicit. If a newly resolved lockfile
# forgot one of them, failing here is preferable to producing an image where
# /usr/bin/wine exists but silently cannot find wineserver or ntdll.dll.
for pkg in wine wine64 wine64-preloader libwine fonts-wine; do
	[ -d "$EX/$pkg" ] || die "$pkg is not staged; add it to packages.seed.txt and rerun 'make -C Vendor/LinuxRuntime resolve fetch'"
	cp -a "$EX/$pkg/." "$UNION/"
done

# Resolve links against the union of all Wine packages. Debian's /usr/bin/wine
# and /usr/bin/wineserver entries may point through /etc/alternatives, while
# their real targets live in wine64; resolving against one package at a time
# would incorrectly resolve those absolute links against the host filesystem.
real_file(){
	local f="$1" target
	if [ -L "$f" ]; then
		target="$(readlink "$f")"
		if [[ "$target" == /* ]]; then
			readlink -f "$UNION$target"
		else
			readlink -f "$f"
		fi
	else
		readlink -f "$f"
	fi
}

copy_one(){
	local f="$1" rel real
	rel="${f#"$UNION"/}"
	real="$(real_file "$f" 2>/dev/null || true)"
	[ -n "$real" ] && [ -f "$real" ] || die "cannot resolve Wine runtime file $rel"
	mkdir -p "$STAGE_DIR/$(dirname "$rel")"
	install -m 0755 "$real" "$STAGE_DIR/$rel"
}

copy_tree(){
	local rel="$1" root="$UNION/$1" f
	[ -d "$root" ] || return 0
	while IFS= read -r -d '' f; do
		copy_one "$f"
	done < <(find "$root" \( -type f -o -type l \) -print0)
}

# wine64 and libwine use both layouts across Debian releases. Copying both is
# harmless and keeps the staging rule valid when the package split changes.
copy_tree usr/lib/wine
copy_tree usr/lib/x86_64-linux-gnu/wine
copy_tree usr/share/wine
copy_tree usr/share/fonts/truetype/wine

# The wrapper tools (wine, winecfg, wineboot, wineserver, regedit, ...) are
# needed by the desktop launcher and by Wine's own helper-process startup.
if [ -d "$UNION/usr/bin" ]; then
	while IFS= read -r -d '' f; do
		copy_one "$f"
	done < <(find "$UNION/usr/bin" -maxdepth 1 \( -type f -o -type l \) \
		\( -name 'wine*' -o -name 'regedit*' -o -name 'regsvr32*' \
		-o -name 'msiexec*' \) -print0)
fi

# Debian normally exposes these through update-alternatives. The target image
# has no dpkg/alternatives database, so provide the canonical entry points
# explicitly after copying the versioned wrappers above.
for tool in wine wineboot winecfg wineconsole winedbg winefile winepath \
		wineserver regedit regsvr32 msiexec; do
	stable="$STAGE_DIR/usr/bin/${tool}-stable"
	[ -f "$stable" ] || continue
	ln -sf "${tool}-stable" "$STAGE_DIR/usr/bin/$tool"
done

# No wine64-preloader in the image. The preloader is a tiny static-pie that
# reserves Wine's fixed low address ranges with MAP_FIXED before loading
# wine64; under ImplusOS that low range is the kernel's identity map, and the
# preloader dies on its first relocation write. ntdll's process creation
# (loader_exec() -> preloader_exec()) falls back to exec'ing wine64 itself
# when the preloader cannot be exec'd, and wine64 started with
# WINELOADERNOEXEC=1 (set by the launcher, inherited by every Windows
# process) does not try to re-exec it. That is the path the first process
# takes too, so remove the preloader rather than leave a binary that crashes.
rm -f "$STAGE_DIR/usr/lib/wine/wine64-preloader" \
      "$STAGE_DIR/usr/lib/wine/wine64-preloader.static" \
      "$STAGE_DIR/usr/lib/x86_64-linux-gnu/wine/wine64-preloader"

# Debian builds Wine's PE modules against the MinGW zlib (ZLIB_PE_LIBS=z), so
# user32.dll imports zlib1.dll, which libz-mingw-w64 ships as an ordinary
# native DLL under /usr/x86_64-w64-mingw32/lib. This build of Wine has no
# system DLL path compiled in, and it ignores DLLs in its own module
# directory unless they carry Wine's builtin signature ("... found in
# WINEDLLPATH but not a builtin, ignoring"), so user32 -- and with it every
# GUI program, winecfg included -- failed to load. Install a copy with the
# signature written into the DOS stub (bytes 0x40.., which only a DOS loader
# would execute) so Wine treats it like its other builtins, both while
# wineboot bootstraps a prefix and afterwards.
ZLIB_DLL="$EX/libz-mingw-w64/usr/x86_64-w64-mingw32/lib/zlib1.dll"
[ -f "$ZLIB_DLL" ] || die "libz-mingw-w64 is not staged (user32.dll needs zlib1.dll); add it to packages.seed.txt and rerun 'make -C Vendor/LinuxRuntime resolve fetch'"
ZLIB_OUT="$STAGE_DIR/usr/lib/x86_64-linux-gnu/wine/x86_64-windows/zlib1.dll"
lfanew=$(od -An -t u4 -j 60 -N 4 "$ZLIB_DLL" | tr -d ' ')
[ "$lfanew" -ge 96 ] || die "zlib1.dll: PE header at $lfanew leaves no DOS stub to sign"
install -m 0644 "$ZLIB_DLL" "$ZLIB_OUT"
printf 'Wine builtin DLL\0' | dd of="$ZLIB_OUT" bs=1 seek=64 conv=notrunc status=none

log "staged x86_64 Wine runtime under $STAGE_DIR"
