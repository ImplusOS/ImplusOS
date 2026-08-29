#!/usr/bin/env bash
# resolve.sh — 外来 Linux バイナリ群の DT_NEEDED 推移的閉包を Debian アーカイブから
#              解決し packages.lock と closure.txt を生成する。
#
# 第三者バイナリは無改変。ここでやるのは「どの .deb が要るか」の特定だけ。
# ネットワーク必須・数百 MB のダウンロードが走る。開発時にのみ実行する。
#
# アルゴリズム（重要）:
#   閉包は「必要な soname にマッチする .so 1 個」だけを辿る。パッケージ内の
#   兄弟 .so（DRI ドライバ、gdb ヘルパ、プラグイン等）は辿らない。これをしないと
#   libgbm → Mesa → libLLVM のように閉包が数百 MB 膨張する。
#
# 入力(env): FOREIGN_BINS（空白区切りの外来バイナリ群。未指定なら CHROME_BIN）
#            DEBIAN_SUITE DEBIAN_SNAPSHOT CACHE_DIR WORK_DIR
#            LOCKFILE CLOSURE SEED
set -euo pipefail

: "${DEBIAN_SUITE:?}" "${DEBIAN_SNAPSHOT:?}"
: "${CACHE_DIR:?}" "${WORK_DIR:?}" "${LOCKFILE:?}" "${CLOSURE:?}" "${SEED:?}"

# 閉包解決の起点にする外来エントリバイナリ群（空白区切り）。
# FOREIGN_BINS が優先。後方互換で CHROME_BIN 単体も受ける。
FOREIGN_BINS="${FOREIGN_BINS:-${CHROME_BIN:-}}"
[ -n "$FOREIGN_BINS" ] || { printf '[resolve] ERROR: FOREIGN_BINS も CHROME_BIN も未指定\n' >&2; exit 1; }

BASE="https://snapshot.debian.org/archive/debian/${DEBIAN_SNAPSHOT}"
DIST="${BASE}/dists/${DEBIAN_SUITE}"

mkdir -p "$CACHE_DIR" "$WORK_DIR"
IDX="$WORK_DIR/index"; mkdir -p "$IDX"
EXTRACT="$WORK_DIR/extract"; rm -rf "$EXTRACT"; mkdir -p "$EXTRACT"

log(){ printf '[resolve] %s\n' "$*" >&2; }
die(){ printf '[resolve] ERROR: %s\n' "$*" >&2; exit 1; }

for _b in $FOREIGN_BINS; do
	[ -r "$_b" ] || die "foreign binary not readable: $_b"
done
command -v readelf >/dev/null || die "readelf missing"
command -v curl    >/dev/null || die "curl missing"

# glibc 本体が供給する soname（閉包の打ち切り点）。libc6 一本に集約。
GLIBC_SONAMES=" ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libmvec.so.1 libdl.so.2 \
libpthread.so.0 librt.so.1 libresolv.so.2 libanl.so.1 libBrokenLocale.so.1 \
libthread_db.so.1 libnss_files.so.2 libnss_dns.so.2 libnss_compat.so.2 "

fetch_url(){ # url dest
	local url="$1" dst="$2"
	[ -s "$dst" ] && return 0
	log "GET ${url##*/}"
	curl -fsSL --retry 3 --retry-delay 2 -o "$dst.part" "$url"
	mv "$dst.part" "$dst"
}

# ---- 1. Packages 索引 ----------------------------------------------------------
PKGS="$IDX/Packages"
if [ ! -s "$PKGS" ]; then
	if curl -fsSL -o "$IDX/Packages.xz" "${DIST}/main/binary-amd64/Packages.xz" 2>/dev/null; then
		xz -dc "$IDX/Packages.xz" > "$PKGS"
	else
		fetch_url "${DIST}/main/binary-amd64/Packages.gz" "$IDX/Packages.gz"
		gzip -dc "$IDX/Packages.gz" > "$PKGS"
	fi
fi
log "Packages: $(wc -l < "$PKGS") lines"

# ---- 2. Contents 索引（path -> package） ------------------------------------
CONT="$IDX/Contents-amd64"
if [ ! -s "$CONT" ]; then
	fetch_url "${DIST}/main/Contents-amd64.gz" "$IDX/Contents-amd64.gz"
	gzip -dc "$IDX/Contents-amd64.gz" > "$CONT"
fi
log "Contents: $(wc -l < "$CONT") lines"

# soname -> package の自動逆引きが transitional / shim（実 .so を含まない
# メタパッケージ）を選んでしまうケースの明示上書き。ここに挙げた soname は
# Contents の最短パス勝ちを無視し、実体を同梱するパッケージへ固定する。
#   libSDL2-2.0.so.0: 最短パス勝ちだと libsdl2-compat-shim（.so 無し）を拾い、
#                     stage.sh の自己検査で "NOT FOUND in extracted debs" になる。
#                     libfluidsynth.so.3 が直接 DT_NEEDED しているため実体が要る。
declare -A SONAME2PKG_OVERRIDE=(
	[libSDL2-2.0.so.0]=libsdl2-2.0-0
)

# soname -> package。ランタイムの実 .so パスだけを対象にし、debug / dev /
# .build-id / dbgsym は除外する。同 soname に複数候補があれば「パスが短い方」
# （＝主実装）を採る。
declare -A SONAME2PKG SONAME2PATHLEN
skip_pkg(){ case "$1" in *-dbg|*-dbgsym|*-dev|*-dev:*|*-udeb) return 0;; *) return 1;; esac; }
while IFS= read -r line; do
	path="${line%% *}"
	case "$path" in
		lib/x86_64-linux-gnu/*.so*|usr/lib/x86_64-linux-gnu/*.so*|lib64/*.so*|usr/lib64/*.so*) : ;;
		*) continue ;;
	esac
	case "$path" in *?debug?*|*.build-id*|*-gdb.py|*.debug) continue ;; esac
	base="${path##*/}"
	pkgfield="${line##* }"
	pkg="${pkgfield##*/}"; pkg="${pkg%%,*}"
	skip_pkg "$pkg" && continue
	plen=${#path}
	if [ -z "${SONAME2PKG[$base]:-}" ] || [ "$plen" -lt "${SONAME2PATHLEN[$base]}" ]; then
		SONAME2PKG[$base]="$pkg"; SONAME2PATHLEN[$base]=$plen
	fi
done < "$CONT"
for _s in "${!SONAME2PKG_OVERRIDE[@]}"; do
	SONAME2PKG[$_s]="${SONAME2PKG_OVERRIDE[$_s]}"; SONAME2PATHLEN[$_s]=0
done
log "soname map entries: ${#SONAME2PKG[@]} (${#SONAME2PKG_OVERRIDE[@]} overridden)"

# ---- 3. Packages から pkg -> version|filename|sha256 -----------------------
declare -A PKG_VER PKG_FILE PKG_SHA
awk '
	BEGIN{RS="";FS="\n"}
	{
		p=v=f=s="";
		for(i=1;i<=NF;i++){
			if($i ~ /^Package: /)  {p=substr($i,10)}
			else if($i ~ /^Version: /){v=substr($i,10)}
			else if($i ~ /^Filename: /){f=substr($i,11)}
			else if($i ~ /^SHA256: /){s=substr($i,9)}
		}
		if(p!=""){print p "\t" v "\t" f "\t" s}
	}' "$PKGS" > "$WORK_DIR/pkgmeta.tsv"
while IFS=$'\t' read -r p v f s; do
	[ -n "${PKG_VER[$p]:-}" ] || { PKG_VER[$p]="$v"; PKG_FILE[$p]="$f"; PKG_SHA[$p]="$s"; }
done < "$WORK_DIR/pkgmeta.tsv"
log "package meta entries: ${#PKG_VER[@]}"

# ---- 4. BFS で閉包を解く --------------------------------------------------
declare -A SEEN_SONAME PKG_SET SONAME_PKG
UNRESOLVED=""       # 改行区切り（空連想配列 + set -u 問題を避ける）
queue=()

needed_of(){ readelf -d "$1" 2>/dev/null | awk -F'[][]' '/\(NEEDED\)/{print $2}'; }

deb_for_pkg(){ # pkg -> cache 内 .deb パス。無ければ取得。
	local pkg="$1" file="${PKG_FILE[$pkg]:-}"
	[ -n "$file" ] || return 1
	local out="$CACHE_DIR/${file##*/}"
	fetch_url "${BASE}/${file}" "$out" >&2
	printf '%s' "$out"
}

extract_pkg(){ # pkg -> 展開ディレクトリを stdout
	local pkg="$1" deb d
	d="$EXTRACT/$pkg"
	if [ ! -d "$d" ]; then
		deb="$(deb_for_pkg "$pkg")" || return 1
		mkdir -p "$d"
		( cd "$d" && ar x "$deb" && \
		  { tar --zstd -xf data.tar.zst 2>/dev/null || tar -xf data.tar.xz 2>/dev/null || \
		    tar -xf data.tar.gz 2>/dev/null || true; } )
	fi
	printf '%s' "$d"
}

# soname にマッチする実ファイル（symlink は実体へ）を展開ツリーから探す
find_so(){ # dir soname -> 実ファイルパス
	local dir="$1" want="$2" f t
	while IFS= read -r f; do
		if [ -L "$f" ]; then
			t="$(readlink -f "$f" 2>/dev/null || true)"
			[ -n "$t" ] && [ -f "$t" ] && { printf '%s' "$t"; return 0; }
		elif [ -f "$f" ]; then
			printf '%s' "$f"; return 0
		fi
	done < <(find "$dir" -name "$want" 2>/dev/null | sort)
	return 1
}

enqueue_needed_of_so(){ # pkg soname : その soname の .so だけを辿る
	local pkg="$1" soname="$2" dir so n
	dir="$(extract_pkg "$pkg")" || { log "no .deb for $pkg"; return 0; }
	so="$(find_so "$dir" "$soname" || true)"
	if [ -z "$so" ]; then
		# soname 名で見つからない場合、SONAME 一致の任意 .so を試す
		while IFS= read -r n; do
			[ "$(readelf -d "$n" 2>/dev/null | awk -F'[][]' '/\(SONAME\)/{print $2}')" = "$soname" ] && { so="$n"; break; }
		done < <(find "$dir" -type f -name '*.so*' 2>/dev/null)
	fi
	[ -n "$so" ] || { log "  ($pkg) file for $soname not found in deb"; return 0; }
	while IFS= read -r n; do [ -n "$n" ] && queue+=("$n"); done < <(needed_of "$so")
}

resolve_queue(){
	while [ "${#queue[@]}" -gt 0 ]; do
		local s="${queue[0]}"; queue=("${queue[@]:1}")
		[ -n "${SEEN_SONAME[$s]:-}" ] && continue
		SEEN_SONAME[$s]=1
		case " $GLIBC_SONAMES " in
			*" $s "*) PKG_SET[libc6]=1; SONAME_PKG[$s]=libc6; continue ;;
		esac
		local pkg="${SONAME2PKG[$s]:-}"
		if [ -z "$pkg" ]; then
			UNRESOLVED+="$s"$'\n'
			log "UNRESOLVED soname: $s"
			continue
		fi
		SONAME_PKG[$s]="$pkg"
		PKG_SET[$pkg]=1
		enqueue_needed_of_so "$pkg" "$s"
	done
}

# 起点: 各外来バイナリの直接 NEEDED
for _b in $FOREIGN_BINS; do
	log "entry binary: $_b"
	while IFS= read -r s; do [ -n "$s" ] && queue+=("$s"); done < <(needed_of "$_b")
done
resolve_queue

# ---- 5. seed の追加パッケージ（dash 等、chrome 依存に出ない実行体） -------
if [ -r "$SEED" ]; then
	while IFS= read -r p; do
		p="${p%%#*}"; p="$(printf '%s' "$p" | tr -d '[:space:]')"
		[ -z "$p" ] && continue
		[ -n "${PKG_VER[$p]:-}" ] || { log "seed package not in index: $p"; continue; }
		PKG_SET[$p]=1
		dir="$(extract_pkg "$p")" || continue
		# パッケージ内の全 ELF（実行体含む・パス不問）の NEEDED を起点に加える。
		# 例: xserver-xorg-core の実体は /usr/lib/xorg/Xorg で bin/ にも .so 名でもない。
		while IFS= read -r f; do
			[ "$(head -c4 "$f" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] || continue
			while IFS= read -r n; do [ -n "$n" ] && queue+=("$n"); done < <(needed_of "$f")
		done < <(find "$dir" -type f 2>/dev/null)
	done < "$SEED"
	resolve_queue
fi

PKG_SET[libc6]=1   # ld-linux + libc は常に必要

# ---- 6. 出力 -----------------------------------------------------------------
{
	echo "# packages.lock — Vendor/LinuxRuntime  (generated by resolve.sh)"
	echo "# date=$(date -u +%FT%TZ)  suite=${DEBIAN_SUITE}  snapshot=${DEBIAN_SNAPSHOT}"
	echo "# base=${BASE}"
	echo "# columns: <package>\\t<version>\\t<deb-url>\\t<sha256>"
	for p in $(printf '%s\n' "${!PKG_SET[@]}" | sort); do
		f="${PKG_FILE[$p]:-}"; v="${PKG_VER[$p]:-}"; sh="${PKG_SHA[$p]:-}"
		[ -n "$f" ] || { echo "# MISSING-IN-INDEX: $p" >&2; continue; }
		printf '%s\t%s\t%s/%s\t%s\n' "$p" "$v" "$BASE" "$f" "$sh"
	done
} > "$LOCKFILE"
log "wrote $LOCKFILE ($(grep -cv '^#' "$LOCKFILE") packages)"

{
	echo "# closure.txt — Vendor/LinuxRuntime  (generated by resolve.sh)"
	echo "# date=$(date -u +%FT%TZ)"
	echo "# columns: <soname>\\t<package>\\t<staged-path>"
	for s in $(printf '%s\n' "${!SONAME_PKG[@]}" | sort); do
		p="${SONAME_PKG[$s]}"
		case "$s" in
			ld-linux-x86-64.so.2) sp="/lib64/$s" ;;
			*)                    sp="/usr/lib/x86_64-linux-gnu/$s" ;;
		esac
		printf '%s\t%s\t%s\n' "$s" "$p" "$sp"
	done
	if [ -n "$UNRESOLVED" ]; then
		echo "# --- UNRESOLVED (要手当て) ---"
		printf '%s' "$UNRESOLVED" | sort -u | sed '/^$/d;s/^/# UNRESOLVED: /'
	fi
} > "$CLOSURE"
nun=$(printf '%s' "$UNRESOLVED" | sort -u | sed '/^$/d' | wc -l)
log "wrote $CLOSURE ($(grep -cv '^#' "$CLOSURE") sonames, $nun unresolved)"
[ "$nun" -eq 0 ] || log "NOTE: 未解決 soname あり。closure.txt 末尾を確認。"
