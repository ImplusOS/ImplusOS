# Vendor/LinuxRuntime/Makefile
#
# 外来 Linux バイナリ用ランタイム（glibc 動的リンカ + 依存 .so 閉包 + locale）の
# 取得・ステージ。設計は ../../Docs/Others/TODO_glibc_Port.md。
#
# 第三者バイナリは無改変。ロジックは *.sh に置き、この Makefile は薄く保つ。
# トップ Makefile からは `linux_runtime_stage` 経由で -C 委譲されるだけ。

REPO_ROOT := $(abspath ../..)
SELF_DIR  := $(abspath .)
HOST_OS   := $(shell uname -s 2>/dev/null || echo Unknown)

# 閉包解決の起点にする外来 Linux バイナリ群（空白区切り）。
#   - chrome   : Chromium 外来 ABI 検証（TODO_Chromium_LinuxABI.md）
#   - linuxxdoom: Userland/Application/Doom（libGL/libGLEW/libopenal/libfluidsynth を追加で要求）
#   - firefox  : Userland/Application/FireFox。エントリを firefox /
#                firefox-bin の 2 本だけにするのは足りない（それらの
#                DT_NEEDED は libc 系だけ）: 本体の依存は同梱 .so 側に出る。
#                libmozsandbox.so が要る librt.so.1 は、起点が chrome と
#                doom だけだった頃は閉包に一度も出ず、画像に載らず
#                "XPCOMGlueLoad error ... librt.so.1: cannot open shared
#                object file" で Firefox が起動しなかった。同梱の .so 全体を
#                起点として並べて、その DT_NEEDED から辿れるようにする。
CHROME_BIN   ?= $(REPO_ROOT)/Userland/Application/Chromium/Resource/chrome
DOOM_BIN     ?= $(REPO_ROOT)/Userland/Application/Doom/Resource/linuxxdoom-x86_64
FIREFOX_BIN  ?= $(REPO_ROOT)/Userland/Application/FireFox/Resource/firefox
FIREFOX_LIBS := $(shell find $(REPO_ROOT)/Userland/Application/FireFox/Resource \
                     \( -name '*.so' -o -name '*.so.*' \) 2>/dev/null)
FOREIGN_BINS ?= $(CHROME_BIN) $(DOOM_BIN) $(FIREFOX_BIN) $(FIREFOX_LIBS)

# Debian アーカイブのスナップショット（再現性のため日付固定）。
# resolve 実行時に packages.lock の先頭へ実際に使った値が書き込まれる。
DEBIAN_SUITE    ?= trixie
DEBIAN_SNAPSHOT ?= 20250901T000000Z

CACHE_DIR := $(SELF_DIR)/cache
WORK_DIR  := $(SELF_DIR)/.work
LICENSE_DIR := $(SELF_DIR)/LICENSES

LOCKFILE  := $(SELF_DIR)/packages.lock
CLOSURE   := $(SELF_DIR)/closure.txt
SEED      := $(SELF_DIR)/packages.seed.txt

# ステージ先（イメージビルドが渡す）。既定は自ツリー内の確認用。
STAGE_DIR ?= $(REPO_ROOT)/Build/x86_64/LinuxRuntime/stage

.PHONY: all resolve fetch stage gtkdata xorgdata fastfetchdata xtermdata alsadata desktopdata locale verify clean help

help:
	@echo "targets: resolve | fetch | stage STAGE_DIR=<d> | gtkdata STAGE_DIR=<d> | xorgdata STAGE_DIR=<d> | fastfetchdata STAGE_DIR=<d> | xtermdata STAGE_DIR=<d> | alsadata STAGE_DIR=<d> | desktopdata STAGE_DIR=<d> | locale STAGE_DIR=<d> | verify | clean"
	@echo "vars:    DEBIAN_SUITE=$(DEBIAN_SUITE) DEBIAN_SNAPSHOT=$(DEBIAN_SNAPSHOT)"
	@echo "         FOREIGN_BINS=$(FOREIGN_BINS)"

# stage を既定にはしない（フェッチが要るため）。トップからは stage を明示的に呼ぶ。
all: help

# 閉包を再解決して packages.lock / closure.txt を更新（開発時のみ・ネットワーク必須）
resolve:
	FOREIGN_BINS='$(FOREIGN_BINS)' \
	DEBIAN_SUITE='$(DEBIAN_SUITE)' DEBIAN_SNAPSHOT='$(DEBIAN_SNAPSHOT)' \
	CACHE_DIR='$(CACHE_DIR)' WORK_DIR='$(WORK_DIR)' \
	LOCKFILE='$(LOCKFILE)' CLOSURE='$(CLOSURE)' SEED='$(SEED)' \
	./resolve.sh

# packages.lock の .deb を cache/ へ取得し sha256 照合
fetch: $(LOCKFILE)
	CACHE_DIR='$(CACHE_DIR)' LOCKFILE='$(LOCKFILE)' ./fetch.sh

# cache の .deb を STAGE_DIR へ展開（実体コピー・symlink 解決）
stage: fetch
	CACHE_DIR='$(CACHE_DIR)' WORK_DIR='$(WORK_DIR)' \
	LOCKFILE='$(LOCKFILE)' CLOSURE='$(CLOSURE)' \
	LICENSE_DIR='$(LICENSE_DIR)' STAGE_DIR='$(STAGE_DIR)' ./stage.sh

# GTK3 外来バイナリ用の非 .so データ（gschemas.compiled / フォント / gtk3-demo 実行体）
# を STAGE_DIR へ配置。stage の後に走らせる（.so 閉包が先）。
gtkdata: stage
	CACHE_DIR='$(CACHE_DIR)' WORK_DIR='$(WORK_DIR)' STAGE_DIR='$(STAGE_DIR)' \
	  ./stage-gtkdata.sh

# Doom(方法A) 用の非 .so データ（Xorg 実行体 / xorg モジュール / Mesa DRI /
# xkb / X コアフォント / xorg.conf）を STAGE_DIR へ配置。stage の後に走らせる。
xorgdata: stage
	CACHE_DIR='$(CACHE_DIR)' WORK_DIR='$(WORK_DIR)' STAGE_DIR='$(STAGE_DIR)' \
	  ./stage-xorg.sh

# FastFetch 用の非 .so データ（/usr/bin/fastfetch 実行体 + presets）を
# STAGE_DIR へ配置。stage の後に走らせる（.so 閉包が先）。
fastfetchdata: stage
	CACHE_DIR='$(CACHE_DIR)' WORK_DIR='$(WORK_DIR)' STAGE_DIR='$(STAGE_DIR)' \
	  ./stage-fastfetch.sh

# 端末エミュレータ用の非 .so データ（/usr/bin/xterm 実行体 + app-defaults +
# terminfo）を STAGE_DIR へ配置。stage の後に走らせる（.so 閉包が先）。
xtermdata: stage
	CACHE_DIR='$(CACHE_DIR)' WORK_DIR='$(WORK_DIR)' STAGE_DIR='$(STAGE_DIR)' \
	  ./stage-xterm.sh

# 閉包中の .deb が持つ .desktop とアプリアイコンを STAGE_DIR へ。これが
# 「デスクトップ環境のアプリ一覧」の供給源になる（stage-desktop.sh 冒頭）。
desktopdata: stage
	CACHE_DIR='$(CACHE_DIR)' WORK_DIR='$(WORK_DIR)' STAGE_DIR='$(STAGE_DIR)' \
	  ./stage-desktop.sh

# alsa-lib の設定（/usr/share/alsa/alsa.conf）を STAGE_DIR へ書き出す。
# libasound.so.2 自体は閉包（stage）に含まれる。
alsadata: stage
	STAGE_DIR='$(STAGE_DIR)' ./stage-alsa.sh

# C.UTF-8 locale-archive を STAGE_DIR/usr/lib/locale に生成
ifeq ($(HOST_OS),Darwin)
locale:
	@echo "locale: skipped on macOS (GNU localedef is required; the Linux runtime stage is complete without it)"
else
locale:
	@command -v localedef >/dev/null || { echo "localedef not found on host" >&2; exit 1; }
	@mkdir -p '$(STAGE_DIR)/usr/lib/locale'
	localedef -i C -f UTF-8 --no-archive '$(STAGE_DIR)/usr/lib/locale/C.UTF-8' || true
	I18NPATH=/usr/share/i18n localedef -i C -f UTF-8 \
	  --prefix='$(STAGE_DIR)' C.UTF-8
	@echo "locale-archive: $$(ls -l '$(STAGE_DIR)/usr/lib/locale/locale-archive' 2>/dev/null || echo MISSING)"
endif

verify:
	CACHE_DIR='$(CACHE_DIR)' LOCKFILE='$(LOCKFILE)' CLOSURE='$(CLOSURE)' \
	STAGE_DIR='$(STAGE_DIR)' ./fetch.sh --verify-only

clean:
	rm -rf $(CACHE_DIR) $(WORK_DIR) $(STAGE_DIR)

$(LOCKFILE):
	@echo "packages.lock がありません。先に 'make -C Vendor/LinuxRuntime resolve' を実行してください。" >&2
	@exit 1
