#!/usr/bin/env bash
# stage-xorg.sh — Doom(方法A) 用の「.so 閉包以外」の X サーバ実行時データを
#                 STAGE_DIR へ配置する。stage.sh（.so 閉包）の後に実行する。
#
#   - Xorg 実行体（Debian trixie xserver-xorg-core の実バイナリ・無改変）→ /usr/bin/Xorg
#   - Xorg モジュール一式（modesetting_drv.so / libglx.so / extensions/*.so …）
#       → /usr/lib/xorg/modules/**   （dlopen されるので DT_NEEDED 閉包に出ない）
#   - Mesa DRI メガドライバ（swrast_dri.so / kms_swrast_dri.so → libgallium-*.so）
#       → /usr/lib/x86_64-linux-gnu/dri/**
#   - xkb データ + xkbcomp … これが無いと Xorg 起動時に XKB 初期化で abort
#   - X コアフォント（misc/ の fixed 等）+ encodings … フォントパス不成立で起動失敗
#   - /etc/X11/xorg.conf … modesetting を /dev/dri/card0 に固定、入力自動追加を無効化
#
# 第三者バイナリは無改変。symlink は実体へ解決してコピー（mtools が symlink 不可）。
#
# 入力(env): CACHE_DIR WORK_DIR STAGE_DIR
set -euo pipefail
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[xorg] %s\n' "$*" >&2; }
die(){ printf '[xorg] ERROR: %s\n' "$*" >&2; exit 1; }

W="$WORK_DIR/xorg"; rm -rf "$W"; mkdir -p "$W"

deb_of(){ ls "$CACHE_DIR"/"$1"_*.deb 2>/dev/null | head -n1; }

extract(){ # tag deb -> dir
	local d="$W/$1"
	mkdir -p "$d"
	( cd "$d" && ar x "$2" && \
	  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
	    tar -xf data.tar.gz 2>/dev/null || die "cannot extract $2"; } )
	printf '%s' "$d"
}

# src ツリーの rel 以下を dst へ実体コピー（symlink は readlink -f で解決）。
copy_tree_real(){ # srcdir dstdir
	local src="$1" dst="$2" f rel t
	[ -d "$src" ] || return 0
	while IFS= read -r f; do
		rel="${f#"$src"/}"
		if [ -L "$f" ]; then
			t="$(readlink -f "$f" 2>/dev/null || true)"
			[ -n "$t" ] && [ -f "$t" ] || continue
			mkdir -p "$dst/$(dirname "$rel")"
			install -m 0644 "$t" "$dst/$rel"
		elif [ -f "$f" ]; then
			mkdir -p "$dst/$(dirname "$rel")"
			install -m 0644 "$f" "$dst/$rel"
		fi
	done < <(find "$src" -type f -o -type l)
}

# ---- 1. Xorg 実行体 + モジュール ------------------------------------------
CORE="$(deb_of xserver-xorg-core)"; [ -n "$CORE" ] || die "xserver-xorg-core deb not in cache"
coredir="$(extract xserver-xorg-core "$CORE")"

mkdir -p "$STAGE_DIR/usr/bin" "$STAGE_DIR/usr/lib/xorg"
# trixie の /usr/bin/Xorg は sh ラッパスクリプト。ImplusOS にシェルは無いので
# 実体 ELF（/usr/lib/xorg/Xorg）を直接 /usr/bin/Xorg と /usr/lib/xorg/Xorg に置く。
xorg_elf=""
for cand in "$coredir/usr/lib/xorg/Xorg" "$coredir/usr/bin/Xorg"; do
	[ -f "$cand" ] || continue
	if [ "$(head -c4 "$cand" | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]; then
		xorg_elf="$cand"; break
	fi
done
[ -n "$xorg_elf" ] || die "no ELF Xorg found in xserver-xorg-core (only wrapper script?)"
install -m 0755 "$xorg_elf" "$STAGE_DIR/usr/bin/Xorg"
install -m 0755 "$xorg_elf" "$STAGE_DIR/usr/lib/xorg/Xorg"
install -m 0755 "$xorg_elf" "$STAGE_DIR/usr/bin/X"

copy_tree_real "$coredir/usr/lib/xorg/modules" "$STAGE_DIR/usr/lib/xorg/modules"
[ -f "$STAGE_DIR/usr/lib/xorg/modules/drivers/modesetting_drv.so" ] \
	|| die "modesetting_drv.so missing (xserver-xorg-core layout changed?)"

# X input driver: evdev (classic EVIOCG* probing, no udev/libinput dependency)
EVDEV="$(deb_of xserver-xorg-input-evdev)"
if [ -n "$EVDEV" ]; then
	evdir="$(extract xserver-xorg-input-evdev "$EVDEV")"
	copy_tree_real "$evdir/usr/lib/xorg/modules/input" "$STAGE_DIR/usr/lib/xorg/modules/input"
fi
[ -f "$STAGE_DIR/usr/lib/xorg/modules/input/evdev_drv.so" ] \
	|| log "WARN: evdev_drv.so missing; keyboard/mouse in X will be unavailable"
[ -f "$STAGE_DIR/usr/lib/xorg/modules/extensions/libglx.so" ] \
	|| log "WARN: libglx.so not found; GLX (Doom needs it) will be unavailable"
log "staged Xorg + $(find "$STAGE_DIR/usr/lib/xorg/modules" -name '*.so' | wc -l) modules"

# Xorg 付属の非実行データ（protocol.txt など）
for extra in usr/share/X11/XErrorDB usr/lib/xorg/protocol.txt; do
	[ -f "$coredir/$extra" ] && { mkdir -p "$STAGE_DIR/$(dirname "$extra")"; \
		install -m 0644 "$coredir/$extra" "$STAGE_DIR/$extra"; }
done

# ---- 2. Mesa DRI メガドライバ（dlopen: /usr/lib/x86_64-linux-gnu/dri/） ----
DRIDEB="$(deb_of libgl1-mesa-dri)"; [ -n "$DRIDEB" ] || die "libgl1-mesa-dri deb not in cache"
dridir="$(extract libgl1-mesa-dri "$DRIDEB")"
copy_tree_real "$dridir/usr/lib/x86_64-linux-gnu/dri" "$STAGE_DIR/usr/lib/x86_64-linux-gnu/dri"
copy_tree_real "$dridir/usr/lib/x86_64-linux-gnu/gbm" "$STAGE_DIR/usr/lib/x86_64-linux-gnu/gbm"
for want in swrast_dri.so kms_swrast_dri.so; do
	[ -f "$STAGE_DIR/usr/lib/x86_64-linux-gnu/dri/$want" ] \
		|| log "WARN: dri/$want missing; software GL path may not load"
done
log "staged DRI: $(ls "$STAGE_DIR/usr/lib/x86_64-linux-gnu/dri" 2>/dev/null | tr '\n' ' ')"

# ---- 2b. EGL (glvnd + Mesa vendor) --------------------------------------
# trixie の dri/*_dri.so は libdril_dri.so (薄い DRI ローダ) への symlink。
# AIGLX/GLX の swrast プロバイダ経路で libdril はサーフェスレス EGL 経由で
# スクリーンを作るため libEGL.so.1 を実行時 dlopen する。DT_NEEDED に出ないので
# .so 閉包(stage.sh)では来ない。ここで明示配置する:
#   libEGL.so.1        <- libegl1      (glvnd ディスパッチ)
#   libEGL_mesa.so.0   <- libegl-mesa0 (実体。glvnd が egl_vendor.d の JSON で発見)
#   /usr/share/glvnd/egl_vendor.d/50_mesa.json  <- 同上（無いと glvnd が vendor 0 個）
EGLDEB="$(deb_of libegl1)"
if [ -n "$EGLDEB" ]; then
	egldir="$(extract libegl1 "$EGLDEB")"
	copy_tree_real "$egldir/usr/lib/x86_64-linux-gnu" "$STAGE_DIR/usr/lib/x86_64-linux-gnu"
fi
EGLMESADEB="$(deb_of libegl-mesa0)"
if [ -n "$EGLMESADEB" ]; then
	eglmesadir="$(extract libegl-mesa0 "$EGLMESADEB")"
	copy_tree_real "$eglmesadir/usr/lib/x86_64-linux-gnu" "$STAGE_DIR/usr/lib/x86_64-linux-gnu"
	copy_tree_real "$eglmesadir/usr/share/glvnd"          "$STAGE_DIR/usr/share/glvnd"
fi
[ -f "$STAGE_DIR/usr/lib/x86_64-linux-gnu/libEGL.so.1" ] \
	|| log "WARN: libEGL.so.1 missing; GLX swrast provider (+iglx) will crash"
[ -f "$STAGE_DIR/usr/lib/x86_64-linux-gnu/libEGL_mesa.so.0" ] \
	|| log "WARN: libEGL_mesa.so.0 missing; glvnd EGL will find no vendor"
[ -f "$STAGE_DIR/usr/share/glvnd/egl_vendor.d/50_mesa.json" ] \
	|| log "WARN: egl_vendor.d/50_mesa.json missing; glvnd EGL vendor discovery fails"
log "staged EGL: $(ls "$STAGE_DIR"/usr/lib/x86_64-linux-gnu/libEGL* 2>/dev/null | xargs -n1 basename 2>/dev/null | tr '\n' ' ')"

# ---- 2c. GLX vendor (glvnd) ---------------------------------------------
# libGLX.so.0 (glvnd) は実際の GLX 実装を libGLX_<vendor>.so.0 として
# **dlopen** する（既定 vendor は "mesa"）。dlopen なので DT_NEEDED に現れず、
# .so 閉包(stage.sh)では絶対に来ない — libgbm / libEGL と同じ穴。
# これが無いと glvnd は vendor 0 個になり、glXCreateContext() が黙って NULL を
# 返す（Doom は "context: (nil)" のあと glXMakeCurrent で GLX BadMatch）。
GLXMESADEB="$(deb_of libglx-mesa0)"
if [ -n "$GLXMESADEB" ]; then
	glxmesadir="$(extract libglx-mesa0 "$GLXMESADEB")"
	copy_tree_real "$glxmesadir/usr/lib/x86_64-linux-gnu" "$STAGE_DIR/usr/lib/x86_64-linux-gnu"
fi
[ -f "$STAGE_DIR/usr/lib/x86_64-linux-gnu/libGLX_mesa.so.0" ] \
	|| log "WARN: libGLX_mesa.so.0 missing; glvnd will have no GLX vendor and glXCreateContext() returns NULL"
log "staged GLX vendor: $(ls "$STAGE_DIR"/usr/lib/x86_64-linux-gnu/libGLX_mesa* 2>/dev/null | xargs -n1 basename 2>/dev/null | tr '\n' ' ')"

# ---- 3. xkb データ + xkbcomp --------------------------------------------
XKB="$(deb_of xkb-data)"; [ -n "$XKB" ] || die "xkb-data deb not in cache"
xkbdir="$(extract xkb-data "$XKB")"
copy_tree_real "$xkbdir/usr/share/X11/xkb" "$STAGE_DIR/usr/share/X11/xkb"

XKBU="$(deb_of x11-xkb-utils)"
if [ -n "$XKBU" ]; then
	xkbudir="$(extract x11-xkb-utils "$XKBU")"
	for b in xkbcomp; do
		[ -f "$xkbudir/usr/bin/$b" ] && install -m 0755 "$xkbudir/usr/bin/$b" "$STAGE_DIR/usr/bin/$b"
	done
fi
[ -f "$STAGE_DIR/usr/bin/xkbcomp" ] || log "WARN: xkbcomp missing; Xorg will fail XKB compile"
log "staged xkb-data + xkbcomp"

# ---- 4. X コアフォント -------------------------------------------------
for p in xfonts-base xfonts-encodings; do
	d="$(deb_of "$p")" || true
	[ -n "${d:-}" ] || { log "WARN: $p not in cache"; continue; }
	fdir="$(extract "$p" "$d")"
	copy_tree_real "$fdir/usr/share/fonts/X11" "$STAGE_DIR/usr/share/fonts/X11"
done
log "staged X core fonts: $(find "$STAGE_DIR/usr/share/fonts/X11" -maxdepth 1 -type d 2>/dev/null | tr '\n' ' ')"

# ---- 5. /etc/X11/xorg.conf -------------------------------------------
mkdir -p "$STAGE_DIR/etc/X11"
cat > "$STAGE_DIR/etc/X11/xorg.conf" <<'CONF'
# ImplusOS 方法A: Xorg を modesetting DDX で /dev/dri/card0（カーネル KMS shim）に固定。
# udev/logind/VT は無いので入力自動追加を切り、レガシー modeset で動かす。
Section "ServerFlags"
    Option "AutoAddDevices" "false"
    Option "AutoAddGPU"     "false"
    Option "DontVTSwitch"   "true"
    Option "DontZap"        "false"
EndSection

Section "Module"
    Load "glx"
    Disable "dri"
    Disable "dri2"
EndSection

Section "InputDevice"
    Identifier  "kbd0"
    Driver      "evdev"
    Option      "Device"   "/dev/input/event0"
    Option      "CoreKeyboard"
EndSection

Section "InputDevice"
    Identifier  "mouse0"
    Driver      "evdev"
    Option      "Device"   "/dev/input/event1"
    Option      "CorePointer"
EndSection

Section "Device"
    Identifier  "kms0"
    Driver      "modesetting"
    Option      "kmsdev"           "/dev/dri/card0"
    Option      "ShadowFB"         "true"
    Option      "Atomic"           "false"
    Option      "AccelMethod"      "none"
    Option      "PageFlip"         "false"
EndSection

Section "Monitor"
    Identifier "mon0"
    Option     "DPMS" "false"
EndSection

Section "Screen"
    Identifier "scr0"
    Device     "kms0"
    Monitor    "mon0"
    DefaultDepth 24
    SubSection "Display"
        Depth   24
        Modes   "1024x768" "800x600" "640x480"
    EndSubSection
EndSection

Section "ServerLayout"
    Identifier "layout0"
    Screen     "scr0"
    InputDevice "kbd0"   "CoreKeyboard"
    InputDevice "mouse0" "CorePointer"
EndSection
CONF
log "wrote /etc/X11/xorg.conf"

log "done: $STAGE_DIR"
