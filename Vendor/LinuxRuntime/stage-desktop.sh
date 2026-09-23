#!/usr/bin/env bash
# stage-desktop.sh — 閉包に入っている .deb が持つ「デスクトップ記述」
#                    （freedesktop.org Desktop Entry とアプリアイコン）を
#                    STAGE_DIR へ配置する。stage.sh の後に走らせる。
#
#   - /usr/share/applications/*.desktop
#   - /usr/share/icons/hicolor/*/apps/*.png
#   - /usr/share/pixmaps/*.png
#
# なぜ要るか: ImplusOS のデスクトップ環境（com.ImplusOS.windowmanager）は
# アプリ一覧をこれらの .desktop から作る（Linux/WM_DesktopEntry.c）。つまり
# 「packages.seed.txt に 1 行足して閉包に入った Debian パッケージの GUI
# アプリは、C を 1 行も書かずにスタートメニューへ出る」ための配線がこれ。
# 逆に言えば、この走査を省くとメニューに出るのは ImplusOS 同梱の記述だけに
# なる。
#
# 対象は「キャッシュにある全 .deb」で、パッケージ名の列挙はしない。閉包が
# 増えたときに自動で付いてくるのが目的なので、明示列挙はその逆をやることに
# なるため。
#
# アイコンは PNG だけを拾う。シェルのデコーダ（stb_image, PNG のみ）で
# 読めるのがそれだけで、Adwaita の scalable/（SVG）や xterm の XPM は
# 置いても描けない。
#
# 入力(env): CACHE_DIR WORK_DIR STAGE_DIR
set -euo pipefail
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${STAGE_DIR:?}"

log(){ printf '[desktopdata] %s\n' "$*" >&2; }

W="$WORK_DIR/desktopdata"; rm -rf "$W"; mkdir -p "$W"

entries=0
icons=0

for deb in "$CACHE_DIR"/*.deb; do
	[ -e "$deb" ] || continue
	# 中身を見ずに展開すると 183 個ぶんの展開コストがかかるので、先に
	# 一覧だけ見て該当するものが無ければ飛ばす。
	#
	# 一覧をファイルに落としてから grep する（パイプで直結しない）のは
	# `set -o pipefail` のため: grep -q は一致した時点で終了し、上流の
	# dpkg-deb は SIGPIPE で死ぬ。パイプライン全体は失敗扱いになるので、
	# `if ! ...` と組み合わせると「該当あり」がそのまま「スキップ」に
	# 化ける（実際それで 0 件になった）。
	dpkg-deb -c "$deb" > "$W/listing.txt" 2>/dev/null || continue
	if ! grep -qE \
		'(usr/share/applications/.*\.desktop|usr/share/pixmaps/.*\.png|usr/share/icons/hicolor/[^/]+/apps/.*\.png)$' \
		"$W/listing.txt"; then
		continue
	fi

	d="$W/$(basename "$deb" .deb)"
	mkdir -p "$d"
	( cd "$d" && ar x "$deb" && \
	  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
	    tar -xf data.tar.gz 2>/dev/null; } ) || { log "WARN: cannot extract $deb"; continue; }

	if [ -d "$d/usr/share/applications" ]; then
		mkdir -p "$STAGE_DIR/usr/share/applications"
		while IFS= read -r f; do
			install -m 0644 "$f" "$STAGE_DIR/usr/share/applications/"
			entries=$((entries+1))
		done < <(find "$d/usr/share/applications" -maxdepth 1 -type f -name '*.desktop')
	fi

	while IFS= read -r f; do
		rel="${f#"$d"/}"
		mkdir -p "$STAGE_DIR/$(dirname "$rel")"
		install -m 0644 "$f" "$STAGE_DIR/$rel"
		icons=$((icons+1))
	done < <(find "$d/usr/share/pixmaps" "$d/usr/share/icons/hicolor" \
		-type f -name '*.png' 2>/dev/null | grep -E '/pixmaps/|/apps/' || true)
done

log "staged $entries desktop entr(y|ies), $icons application icon(s)"
