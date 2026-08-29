# ImplusOS — Doom 実行プラン（方法A: 無改変 Xorg + ソフト GL）

> **ステータス: 2026-08-29。**
> **P1（ランタイム取得・ステージ・ランチャ配線・envp/cwd）完了・検証済み。**
> **M1〜M5（カーネル: /tmp tmpfs・/dev/dri/card0 char device・Linux DRM/KMS
> エミュレーション・デバイス fd mmap・/dev/input/event* + evdev ioctl）
> 実装済み・`make kernel` 通過。ただし QEMU 未ブートのため動作は未検証。**
> **M6（Xorg 立ち上げ反復 + 実描画確認）未了 — QEMU ブートループが必須で、
> 本セッション環境（カーネルビルドのみ可・ブート不可）では到達できない。**

調査基準日: 2026-08-29
対象バイナリ: `Userland/Application/Doom/Resource/linuxxdoom-x86_64`
（[EVV1E/DOOM](https://github.com/EVV1E/DOOM) の無改変ビルド、GPLv2）

関連: [`TODO_glibc_Port.md`](TODO_glibc_Port.md)（ランタイム同梱機構）、
[`TODO_Chromium_LinuxABI.md`](TODO_Chromium_LinuxABI.md)（Linux syscall 互換層）、
[`TODO_GTK3_Wayland_LinuxABI.md`](TODO_GTK3_Wayland_LinuxABI.md)（同型の外来 GUI 実行）。

---

## 0. 制約と到達点の定義

**制約（ユーザ指定）**: Doom バイナリは無改変。外部ソースコード追加なし。
追加できるのは「ビルド済みバイナリ／データファイル」と ImplusOS 側の配線のみ。

`linuxxdoom-x86_64` は無改変だと必ず **X11 + GLX クライアント**として動く
（`DT_NEEDED`: `libX11` `libm` `libopenal.so.1` `libGL.so.1` `libGLEW.so.2.2`
`libfluidsynth.so.3` `libasound` `libc`）。よって実画面到達には
(1) X サーバ本体 と (2) GLX が通る GL ドライバ の両方が要り、少なくとも一方が
Linux デバイスノード経由で ImplusOS のフレームバッファへ書く必要がある。

**方法A**: Debian trixie の **Xorg（modesetting DDX 内蔵・無改変）** ＋
**Mesa ソフト GL（llvmpipe / kms_swrast）** をビルド済みのまま同梱し、カーネルの
KMS shim を `/dev/dri/card0` として Linux ioctl/mmap ABI で見せて接続する。
Doom はただの GLX クライアントとして起動する。

| マイルストン | 到達点 |
|---|---|
| **P1（済）** | ランタイム全依存 `.so` 閉包 + Xorg/Mesa/xkb/フォント/`xorg.conf` を OS イメージへステージ。ランチャが Xorg→Doom の順に spawn。ホストで `ld.so --library-path <stage> linuxxdoom` が依存解決まで進む。 |
| **P2** | ImplusOS 上で `Xorg :0` が `/dev/dri/card0` を開き、`GETRESOURCES`→`SETCRTC` までログが進む。 |
| **P3** | `linuxxdoom` が `XOpenDisplay(":0")` 成功、`glXCreateContext`（llvmpipe）成功、`glewInit()` 通過。 |
| **P4** | Doom タイトル画面が ImplusOS のコンソールに描画される（`PAGE_FLIP`→`display_present()`）。 |
| **P5** | `/dev/input/event*` 経由でキーボード入力が Xorg→Doom に届き、操作可能。 |

---

## 1. P1 でやったこと（このコミット）

| パス | 種別 | 内容 |
|---|---|---|
| `Vendor/LinuxRuntime/resolve.sh` | 変更 | 閉包起点を `CHROME_BIN` 単体 → `FOREIGN_BINS`（空白区切り）に一般化。`SONAME2PKG_OVERRIDE` 追加（`libSDL2-2.0.so.0` を実体入り `libsdl2-2.0-0` に固定：`libfluidsynth.so.3` が直接 NEEDED、`libsdl2-compat-shim` は `.so` 無し）。 |
| `Vendor/LinuxRuntime/Makefile` | 変更 | `DOOM_BIN` / `FOREIGN_BINS` 追加。`xorgdata` ターゲット追加。 |
| `Vendor/LinuxRuntime/resolve.sh` | 変更（再掲） | seed パッケージの ELF 走査をパス不問（`file` マジック判定）に拡大。`/usr/lib/xorg/Xorg` のような bin/ にも `.so` 名でもない実体の NEEDED（`libunwind.so.8` 等）を取りこぼさないため。 |
| `Vendor/LinuxRuntime/packages.seed.txt` | 変更 | Doom 直接依存（`libgl1` `libglx0` `libglvnd0` `libglew2.2` `libopenal1` `libfluidsynth3` `libsdl2-2.0-0`）＋ X サーバスタック（`xserver-xorg-core` `xserver-xorg-legacy` `libgl1-mesa-dri` `libglx-mesa0` `libxcvt0` `libxfont2` `xkb-data` `x11-xkb-utils` `xfonts-base` `xfonts-encodings` `xfonts-utils`）を seed に追加。`libunwind8` `libaudit1` `libcap-ng0` は Xorg 実体から推移的に取得。 |
| `Vendor/LinuxRuntime/packages.lock` | 再生成 | 119 → **170** パッケージ（trixie / snapshot `20250901T000000Z` ピン、sha256 込み）。 |
| `Vendor/LinuxRuntime/closure.txt` | 再生成 | 133 → **177** soname、未解決 **0**。 |
| `Vendor/LinuxRuntime/stage-xorg.sh` | 新規 | `.so` 閉包以外の X 実行時データを配置：`/usr/bin/Xorg`、`/usr/lib/xorg/modules/**`（`modesetting_drv.so` / `extensions/libglx.so` ほか）、`/usr/lib/x86_64-linux-gnu/dri/**`（`swrast_dri.so` / `kms_swrast_dri.so`）、`/usr/share/X11/xkb/**` + `xkbcomp`、`/usr/share/fonts/X11/{misc,encodings}/**`、`/etc/X11/xorg.conf`。 |
| `Makefile` | 変更（1 行） | `linux_runtime_stage` の `-C` 呼び出しに `xorgdata` を追加。 |
| `Userland/Application/Doom/Start.c` | 変更 | `BUSYBOX_PATH`→`DOOM_PATH` 改名。`/usr/bin/Xorg :0 -config /etc/X11/xorg.conf -nolisten tcp -novtswitch -keeptty +iglx -logfile /dev/shm/Xorg.0.log` を spawn → `sleep_ms(4000)` → Doom spawn → Doom 終了で Xorg を kill。 |
| `Userland/Application/Doom/Resource/fetch-wad.sh` | 新規 | DOOM シェアウェア IWAD を取得（md5/size/マジック検証）。 |
| `Userland/Application/Doom/Makefile` | 変更 | `all:` に `wad` ターゲット追加（初回のみ自動取得、失敗しても継続）。 |
| `Userland/Application/Doom/.gitignore` | 新規 | `doom1.wad` / `soundfont.sf2` を非コミット。 |
| `Kernel/Core/process/ProcessManager_Create.c` | 変更 | (a) 外来 ELF spawn 時、子プロセスの cwd を実行体のあるディレクトリに設定（`IdentifyVersion()` の `access("./doom1.wad")` 対策）。(b) `glibc_envp` に `LIBGL_ALWAYS_SOFTWARE=1` `GALLIUM_DRIVER=llvmpipe` `XKB_CONFIG_ROOT=/usr/share/X11/xkb` `DOOMWADDIR=/Userland/Doom/Resource` `SOUNDFONT=/Userland/Doom/Resource/soundfont.sf2` を追加。 |
| `Kernel/IPC/UnixSocket.c` | 変更（1 行） | 先行の未コミット編集に `EPOLL*/POLL*` というコメント内 `*/` があり早期クローズしてビルド不能だったのを修正（`EPOLL / POLL`）。方法A とは独立の既存バグ。 |

検証:
```
make -C Vendor/LinuxRuntime resolve   # 177 sonames, 0 unresolved
make -C Vendor/LinuxRuntime stage     # placed=177 missing=0
make -C Vendor/LinuxRuntime xorgdata  # Xorg(ELF実体) + 11 modules + DRI + xkb + fonts + xorg.conf
make -C Vendor/LinuxRuntime verify    # closure satisfied
make kernel                           # OK（cwd + envp 変更込み）
make -C Userland/Application/Doom      # Doom.ELF OK
```
ステージ済みツリーへの素振り（ホスト x86_64 Linux、実体のみ使用）:
```
S=Build/x86_64/LinuxRuntime/stage
$S/lib64/ld-linux-x86-64.so.2 --library-path $S/usr/lib/x86_64-linux-gnu $S/usr/bin/Xorg -version
  -> "X.Org X Server 1.21.1.16" が表示（Xorg の .so 閉包充足）
(cd Userland/Application/Doom/Resource && .../ld-linux-x86-64.so.2 --library-path .../x86_64-linux-gnu ./linuxxdoom-x86_64)
  -> "W_InitFiles: no files found"（WAD 未配置。全 .so は解決済み・実行到達）
```
Doom + Xorg + 全 xorg モジュール + 全ステージ `.so` の DT_NEEDED 再帰チェック → 未充足 **0**。

---

## 2. ランタイムのフレーム経路（P4 到達時）

```
linuxxdoom  --glXSwapBuffers-->  libGL(glvnd) --> libGLX_mesa --> libgallium(llvmpipe)
   |                                                                   |
   |  X11 protocol (unix:/tmp/.X11-unix/X0)              kms_swrast: CREATE_DUMB/MAP_DUMB
   v                                                                   v
 Xorg (modesetting DDX, ShadowFB)  --DRM ioctl(/dev/dri/card0)-->  カーネル DRM/KMS shim
   |  DamageReport -> shadow blit -> DRM_IOCTL_MODE_DIRTYFB / PAGE_FLIP
   v
 カーネル: dumb BO --memcpy--> display_get_framebuffer()  ; display_present()
   v
 ImplusOS コンソール画面
```

---

## 3. カーネル作業（M1〜M5 実装済み・`make kernel` 通過・QEMU 未検証 / M6 未了）

### 実装済みの変更（このコミット、B: ブラインド実装）

| ファイル | 変更 |
|---|---|
| `Kernel/Core/vfs/TmpFS.c` | フラット名前空間を `/dev/shm` に加え `/tmp` `/run` でも受理（`tmpfs_path_ok`）。`mkdir` は tmpfs 配下なら成功（ディレクトリは暗黙）。`opendir`/`readdir` を任意サブディレクトリ対応（直下の子だけ列挙）。`TMPFS_MAX_FILES` 64→256。 |
| `Kernel/Core/kernel_main.c` | `vfs_mount("/tmp",...)` / `vfs_mount("/run",...)`。`drm_kms_init()`。 |
| `Kernel/include/kernel/interfaces/vfs_types.h` | `vfs_driver_t` に任意フック `dev_ioctl` / `dev_read` / `dev_poll` / `dev_mmap` を追加（既存 FS 無影響）。 |
| `Kernel/Core/vfs/VFS.c` / `VFS.h` | `vfs_file_is_chardev` + `vfs_dev_{ioctl,read,poll,mmap}` ラッパ。 |
| `Kernel/Core/vfs/DevFS.c` | ノード追加: `/dev/dri/card0`・`/dev/dri/renderD128`・`/dev/input/event0`・`/dev/input/event1`。`dev_*` フック実装（DRM は `drm_kms_*`、evdev は `evdev_*` に委譲、read は user バウンス）。close で `drm_kms_close()`。 |
| `Kernel/Core/drm/DRM_Kms.c` / `.h`（新規） | 実 Linux DRM/KMS。`_IOC` デコード + `drm_mode.h` レイアウト。VERSION / GET_CAP / SET_CLIENT_CAP / GETRESOURCES(2パス) / GETCONNECTOR(+modeinfo) / GETENCODER / GET・SETCRTC / CREATE・MAP・DESTROY_DUMB / ADDFB・ADDFB2・RMFB・GETFB / PAGE_FLIP(+flip-complete イベント) / DIRTYFB / GETPLANERES・GETPLANE / OBJ_GETPROPERTIES / ATOMIC→`-EOPNOTSUPP`（legacy 強制）/ GEM_CLOSE。dumb BO は `pmm_alloc_pages`、`PAGE_FLIP`/`DIRTYFB`/`SETCRTC` で `display_get_framebuffer()` へブリット + `display_present()`。read で `drm_event_vblank` を排出。 |
| `Kernel/Core/syscall/Syscall_File.c` / `.h` | `used==1` fd で driver が `dev_read`/`dev_poll` を持てばそちらへ。`syscall_file_is_chardev` / `syscall_file_ioctl` / `syscall_file_dev_mmap` を追加。 |
| `Kernel/Compat/Linux/Syscall_LinuxCompat.c` | `syscall_ioctl_ex`: chardev fd を `syscall_file_ioctl` へ（`arg==0` ガードより前）。`linux_mmap`: chardev fd を `syscall_file_dev_mmap` へ。 |
| `Kernel/Core/process/ProcessManager_Create.c` | 外来 ELF spawn 時の子 cwd = 実行体ディレクトリ。`glibc_envp` に `LIBGL_ALWAYS_SOFTWARE` / `GALLIUM_DRIVER=llvmpipe` / `XKB_CONFIG_ROOT` / `DOOMWADDIR` / `SOUNDFONT`。 |
| `Kernel/Drivers/Module/Evdev_Client.c` | `evdev_ioctl` を stub から実装（`EVIOCGVERSION`/`EVIOCGID`/`EVIOCGNAME`/`EVIOCGPHYS`/`EVIOCGPROP`/`EVIOCGBIT(0/KEY/REL)`/`EVIOCGKEY・LED・SW`/`EVIOCGABS`/`EVIOCGRAB`/`EVIOCSCLOCKID`）。dev0=キーボード, dev1=相対ポインタ。 |
| `Vendor/LinuxRuntime/packages.seed.txt` ほか | `xserver-xorg-input-evdev` 追加（lock 173 pkg / closure 179 soname / 未解決 0）。`stage-xorg.sh` が `evdev_drv.so` 同梱 + `xorg.conf` に InputDevice(kbd0/mouse0→/dev/input/event0,1) 追加。 |

**未検証の既知リスク（QEMU で潰す）**: `drm_kms_mmap` の `process_user_reserve`+`paging_map_user_page` による BO マップ実効性 / dumb BO を `process_user_alloc` でなく物理ページにしたことと Xorg 単一プロセス前提の整合 / `drm_kms_read` が user ポインタ前提（`linux_read` は user ポインタを直接渡す = OK のはず）/ TmpFS フラット名前空間で X のロックファイル生成 / evdev bitmap の網羅性 / llvmpipe JIT の `PROT_EXEC`（`syscall_vm_mprotect` は EXEC 対応済み）。

### M1. `/tmp` と `/run` を tmpfs に  — 実装済み

**現状**: `TmpFS` は `/dev/shm` のみ（`Kernel/Core/vfs/TmpFS.c` `TMPFS_PREFIX`）。
`/tmp` はどの FS にもマウントされていない。`glibc_envp` は `HOME=/tmp`
`XDG_RUNTIME_DIR=/tmp` を指しているが実体が無い。
**X の UNIX ソケットパス `/tmp/.X11-unix/X0` は libX11/Xserver にコンパイル時固定**で、
無改変では変えられない。

**作業**:
- `TmpFS` を複数プレフィックスに対応させる（`/dev/shm` `/tmp` `/run`）か、`/tmp`
  専用にもう 1 インスタンス mount する（`kernel_main.c` の vfs_mount 群）。
- `TmpFS` にサブディレクトリ作成を実装（`mkdir("/tmp/.X11-unix")`、`mkdir("/tmp/.cache")` …）。
  現状 `tmpfs_vfs_mkdir` の有無・ネストパス対応を確認。上限（`TMPFS_MAX_FILES 64` /
  `TMPFS_PATH_MAX 256`）を X 用に引き上げ。
- AF_UNIX 抽象名前空間（`@/tmp/.X11-unix/X0`、先頭 `\0`）対応があれば socket ファイル
  実体無しでも可。`Kernel/IPC/UnixSocket.c` の bind/connect はパス文字列比較なので
  抽象アドレス（NUL 始まり、長さ指定）を扱えるよう小改修すれば M1 の FS 部分を回避可。

**確認**: `Xorg` 起動ログに `unix:/tmp/.X11-unix/X0` の bind 成功、`ls /tmp/.X11-unix`。

### M2. `/dev/dri/card0` キャラクタデバイス + fd ルーティング

**現状**: `Kernel/Core/drm/DRM_Client.c` は `SYSCALL_DRM_*`（206–210）という
**ImplusOS ネイティブ syscall 専用**で、fd base は偽の `0x6000`。`open("/dev/dri/card0")`
からは到達不能。`DevFS`（`Kernel/Core/vfs/DevFS.c`）は `null/zero/full/urandom/random/tty`
のみ。`vfs_driver_t` に `ioctl`/`mmap` フックが無い。Linux 互換層
（`Syscall_LinuxCompat.c` `syscall_ioctl_ex`）は TTY ioctl だけ。

**作業**:
1. `DevFS` に `/dev/dri/card0`（と将来用 `/dev/dri/renderD128`、`/dev/fb0`）を追加。
   `devfs_entry_t` に `kind = DEVFS_KIND_DRM_CARD` を新設。
2. デバイス fd バックエンドの概念を fd レイヤに導入。最小案:
   - `file_t`（`vfs_types.h`）にデバイス種別タグを持たせる、または
   - `Syscall_File.c` / `Syscall_LinuxCompat.c` の `read`/`write`/`ioctl`/`mmap`/`close`/`poll`
     ハンドラで「この fd が DevFS の DRM ノードなら `drm_kms_*()` に委譲」する分岐を追加。
   - `vfs_driver_t` に任意フック `int64_t (*ioctl)(vfs_file_t*, uint64_t req, uint64_t arg)` /
     `int64_t (*mmap)(vfs_file_t*, uint64_t off, uint64_t len, uint32_t prot, void** out)` /
     `int64_t (*poll)(vfs_file_t*, uint32_t events)` を足すのが素直（NULL 可・既存 FS 無影響）。
3. Linux 互換の `open` は既に VFS 経由で `/dev/*` を引ける（`/dev/null` 等が動いている）
   ので、`/dev/dri/card0` の `find_file` を返せば `open` は通る。`O_CLOEXEC`/`O_NONBLOCK`
   フラグ保持を確認。

**確認**: Linux バイナリから `open("/dev/dri/card0", O_RDWR)` が >= 0 を返す。

### M3. 実 Linux DRM/KMS ioctl エミュレーション（`DRM_Client.c` を置換）

**現状の `drm_client_ioctl` は使い物にならない**:
- 要求番号が偽物（`DRM_IOCTL_VERSION 0xC0` 等）。実際の Linux は `_IOC` エンコード
  （例 `DRM_IOWR(0x00, struct drm_version)` = `0xC0186400`、
  `DRM_IOWR(0xA0, struct drm_mode_card_res)` = `0xC04064A0`）。
- 構造体レイアウトが実物と不一致（`drm_mode_get_connector` / `drm_mode_modeinfo` /
  `drm_mode_card_res` …）。

**作業**: `Kernel/Core/drm/DRM_Kms.c`（新規、`DRM_Client.c` は削除か native 専用に隔離）。
`<drm/drm.h>` `<drm/drm_mode.h>` のレイアウトに正確に合わせる。`_IOC` の
dir/type/nr/size を自前デコード。既存の `display_get_framebuffer()` へのブリットは流用。

必要な ioctl（modesetting DDX + kms_swrast + libdrm が投げるもの）:

| ioctl | 構造体 | 実装要点 |
|---|---|---|
| `DRM_IOCTL_VERSION` | `drm_version` | name=`"implusdrm"` 相当、`*_len` 2 パス（長さ問い合わせ→本体）対応必須 |
| `DRM_IOCTL_GET_CAP` | `drm_get_cap` | `DRM_CAP_DUMB_BUFFER=1`、`DRM_CAP_CRTC_IN_VBLANK_EVENT=1`、`PRIME=0`、`ADDFB2_MODIFIERS=0` |
| `DRM_IOCTL_SET_CLIENT_CAP` | `drm_set_client_cap` | `UNIVERSAL_PLANES` / `ATOMIC` を受理（0 でも可、`Atomic"false"` なので） |
| `DRM_IOCTL_SET_MASTER` / `DROP_MASTER` | — | 0 |
| `DRM_IOCTL_MODE_GETRESOURCES` | `drm_mode_card_res` | crtc/connector/encoder 各 1、fb 0。2 パス（count 問い合わせ→ID 配列書き込み）対応必須 |
| `DRM_IOCTL_MODE_GETCONNECTOR` | `drm_mode_get_connector` | connected、`count_modes=1`（`display_width/height()` から 1 モード）、`count_encoders=1`、`count_props` は 0 でも DDX は動く。2 パス対応必須。`drm_mode_modeinfo` を 1 個返す |
| `DRM_IOCTL_MODE_GETENCODER` | `drm_mode_get_encoder` | `encoder_id=1`、`crtc_id=1`、`possible_crtcs=1` |
| `DRM_IOCTL_MODE_GETCRTC` | `drm_mode_crtc` | 初期は `mode_valid=0` |
| `DRM_IOCTL_MODE_SETCRTC` | `drm_mode_crtc` | `fb_id`/`mode`/`set_connectors_ptr` を記録。以降の `PAGE_FLIP`/`DIRTYFB` の宛先 |
| `DRM_IOCTL_MODE_GETPLANERESOURCES` / `GETPLANE` | `drm_mode_get_plane_res` / `drm_mode_get_plane` | primary plane 1 個（`UNIVERSAL_PLANES` を返した場合） |
| `DRM_IOCTL_MODE_OBJ_GETPROPERTIES` / `GETPROPERTY` | — | 空（`count=0`）で可（legacy パス） |
| `DRM_IOCTL_MODE_CREATE_DUMB` | `drm_mode_create_dumb` | `process_user_alloc`（既存ロジック流用）。pitch=`width*bpp/8` |
| `DRM_IOCTL_MODE_MAP_DUMB` | `drm_mode_map_dumb` | `offset` に「mmap 用の擬似オフセットトークン」を返す（M4 が解決）。**現状の「カーネルポインタをそのまま返す」実装は Linux プロセスでは無効**なので置換 |
| `DRM_IOCTL_MODE_DESTROY_DUMB` | `drm_mode_destroy_dumb` | 解放 |
| `DRM_IOCTL_MODE_ADDFB` / `ADDFB2` | `drm_mode_fb_cmd` / `_cmd2` | handle→fb_id。`_cmd2` は plane[0]/pitch[0] を見る |
| `DRM_IOCTL_MODE_RMFB` | uint32 | 解放 |
| `DRM_IOCTL_MODE_PAGE_FLIP` | `drm_mode_crtc_page_flip` | dumb BO を `display_get_framebuffer()` へブリット→`display_present()`。`DRM_MODE_PAGE_FLIP_EVENT` 指定時は完了 `drm_event_vblank` を fd の read キューへ積む |
| `DRM_IOCTL_MODE_DIRTYFB` | `drm_mode_fb_dirty_cmd` | ShadowFB 経路の実描画。矩形リストぶんだけ部分ブリット |
| `DRM_IOCTL_MODE_ATOMIC` | `drm_mode_atomic` | `Atomic"false"` 前提なので `-EOPNOTSUPP`（`-95`）で可 |
| `DRM_IOCTL_GEM_CLOSE` | `drm_gem_close` | handle 解放 |
| `DRM_IOCTL_MODE_CREATE_LEASE` 等 | — | `-EINVAL` |

fd の `read()`（`drmHandleEvent` が `PAGE_FLIP_EVENT` 完了を待つ）: `drm_event_vblank`
（`struct drm_event` ヘッダ + `user_data`）をノンブロッキング/ブロッキングで返す。
`poll()`: 完了イベントがキューにあれば `POLLIN`。

### M4. DRM fd の `mmap`（dumb BO）+ llvmpipe 用 `PROT_EXEC` mmap

**作業**:
- `MAP_DUMB` が返したオフセットトークンで `mmap(fd=/dev/dri/card0, offset=token)` が来たら、
  対応する dumb BO の物理ページを **呼び出しプロセスのユーザ空間**に実マップする
  （`Syscall_LinuxCompat.c` `linux_mmap` にデバイス fd 分岐を追加。`process_user_*` /
  ページテーブル API を使用）。現在の `drm_client_mmap` は `return (void*)offset` で
  カーネルポインタを渡すだけ＝Linux プロセスからは触れない。要置換。
- **llvmpipe は LLVM MCJIT で実行可能メモリを要求する**。`mmap(PROT_EXEC)` /
  `mprotect(...PROT_EXEC)` がユーザ空間で機能すること（W^X を保つなら
  `PROT_WRITE`→`PROT_EXEC` の遷移を許可）。`Syscall_VM.c` / `linux_mmap` / `linux_mprotect`
  を確認・対応。ここが無いと `glXCreateContext` 前後で SIGSEGV/`llvm::...` abort。
  代替: `GALLIUM_DRIVER=softpipe`（JIT 無し・激遅だが可）を envp で切替可能にしておく。

### M5.（P5 用）`/dev/input/event*` を Xorg に

`Kernel/Drivers/Module/Evdev_Client.c` に evdev リングは既にある（`SYSCALL_EVDEV_*`、
fd base `0x7000`、キーボード=dev0 / マウス=dev1）。M2 と同じ要領で
`/dev/input/event0` `/dev/input/event1` を `DevFS` に追加し `read`/`ioctl`（`EVIOCGBIT`
`EVIOCGABS` `EVIOCGNAME` `EVIOCGID` 等）を `evdev_ioctl` に委譲。
`xorg.conf` に `InputDevice`（`Driver "libinput"` は udev 依存なので `"evdev"` を明示、
無ければ `xserver-xorg-input-evdev` を seed 追加）または `-config` で
`Option "Device" "/dev/input/event0"`。`AutoAddDevices"false"` のままなら明示セクション必須。
P4 までは入力不要（Doom はタイトル画面まで進む）。

### M6. Xorg 立ち上げ反復 — QEMU 実測ログ（2026-08-29 1回目）

初回ブートで判明・対処済み:

| 症状 | 原因 | 対処（このコミット） |
|---|---|---|
| `Linking lock file (/tmp/.X0-lock) in place failed: Function not implemented` で X が即 fatal | `link(2)`/`linkat(2)` が `-ENOSYS`。X の `LockServer()` は `/tmp/.tXn-lock` を `link()` で `/tmp/.Xn-lock` へ置く | `syscall_file_link()` 新規（pseudo-fs 限定・内容コピーで link を模倣）＋ `LINUX_SYS_LINK`/`LINKAT` dispatch |
| Doom が `I_Init` 付近で `libpulse` の `pa_make_fd_cloexec()` assert → `Aborting.` | `fcntl(sockfd, F_GETFD)` がソケット fd で負値。libopenal がロードした PipeWire/Pulse バックエンドが接続ソケットに対して呼ぶ | `syscall_fcntl_ex` の `F_GETFD`/`F_SETFD` をソケット fd 対応（0 を返す/受理）＋ `glibc_envp` に `ALSOFT_DRIVERS=null` `SDL_AUDIODRIVER=dummy` `PULSE_SERVER=none`（音は無音で継続） |
| `Failed to create secure directory (/tmp/pulse)` | TmpFS の `mkdir` は成功を返すが実ディレクトリを `stat` できずパーミッション検査に落ちる | 上記の `ALSOFT_DRIVERS=null` で Pulse 経路自体を無効化して回避 |

**2回目ブート**（audio は `ALSOFT_DRIVERS=null` で通過、Doom は `ST_Init` まで到達し
`Couldn't connect to display!` → X 未起動のため。X は下記で fatal）:

| 症状 | 原因 | 対処（このコミット） |
|---|---|---|
| `_XSERVTransSocketOpenCOTSServer: Unable to open socket for local` / `unix` → `Cannot establish any listening sockets` で X fatal | `socket(AF_UNIX, SOCK_STREAM)` が `-ENOMEM`。グローバル `g_usocks` テーブル（`UNIX_SOCK_MAX=16`）が枯渇。**プロセス終了時に AF_UNIX ソケットを解放していなかった**（`syscall_file_close_all_for_pid` / `syscall_socket_close_all_for_pid` はあるが unix 版が無い）ため、アプリ起動のたびにスロットがリーク | `unix_socket_close_all_for_pid()` 新規＋プロセス終了処理から呼び出し。`UNIX_SOCK_MAX` 16→64。 |

`-logfile /dev/tty` に変更済み（DevFS `/dev/tty`→COM1）。次ブートで X の全ログがシリアルに出る。
次に想定される壁: `xkbcomp` fork/exec、Mesa llvmpipe の実行可能 `mmap`、`/dev/dri/card0` の DRM ハンドシェイク、フォントパス。

### M6（旧・想定リスト）

`/dev/shm/Xorg.0.log` を QEMU 経由で回収し、以下を潰す:
- ミッシングモジュール（`Failed to load module`）→ `stage-xorg.sh` に追加。
- `xkbcomp` 失敗 → `XKB_CONFIG_ROOT` / `xkbcomp` パス。`-xkbdir` 明示も可。
- フォントパス（`could not open default font 'fixed'`）→ `xfonts-base` の `fonts.dir` 同梱確認、
  `-fp /usr/share/fonts/X11/misc` 明示。
- GLX 不成立（`GLX: Initialized DRISWRAST` が出ない）→ `+iglx` は指定済み。
  `libglx.so` ロード確認、`Section "Module" Load "glx"`。indirect GLX で
  `linuxxdoom` の `glXChooseVisual` が通ればよい（直接不可でも可）。
- `Atomic` / `PageFlip` は `xorg.conf` で無効化済み（legacy `SETCRTC`+`DIRTYFB` 経路）。

---

## 4. 検証手順

**ホスト素振り（P1 相当・カーネル不要）**:
```
S=Build/x86_64/LinuxRuntime/stage
$S/lib64/ld-linux-x86-64.so.2 --library-path \
  $S/usr/lib/x86_64-linux-gnu $S/usr/bin/Xorg -version
$S/lib64/ld-linux-x86-64.so.2 --library-path \
  $S/usr/lib/x86_64-linux-gnu Userland/Application/Doom/Resource/linuxxdoom-x86_64 -?
```

**QEMU（P2〜P4）**: `make image_livecd && make run_uefi_cdrom`。COM1 シリアルに
`Xorg.0.log` を tee するか `/dev/shm/Xorg.0.log` を Doom 終了後にシリアルダンプ。
チェックポイント: `card0 opened` → `GETRESOURCES` → `SETCRTC` → `X: display :0 ready`
→ `linuxxdoom: XOpenDisplay ok` → `glewInit ok` → 画面。

---

## 5. データファイル（バイナリ／データ追加のみ・ソース不要）

| ファイル | 置き場所 | 状態 |
|---|---|---|
| `doom1.wad` | `Userland/Application/Doom/Resource/doom1.wad` | **配線済み**。`Resource/fetch-wad.sh` が DOOM シェアウェア IWAD v1.9（size 4196020 / md5 `f0cefca49926d00903cf57551d901abe`、`IWAD` マジック検証、自由再配布可）を取得。Doom の `Makefile` の `all:` が `wad` ターゲット経由で初回のみ自動取得（オフライン時は警告して続行）。`.gitignore` 済み（`Vendor/LinuxRuntime/cache` と同じ非コミット運用）。`DOOMWADDIR=/Userland/Doom/Resource`（envp）＋ 子 cwd がこのディレクトリを指す。 |
| `soundfont.sf2` | `Userland/Application/Doom/Resource/soundfont.sf2` | 任意。FluidSynth 音楽用。無くても `i_sound.c` は継続（致命的でない）。`SOUNDFONT` envp は設定済み。 |

**ホスト素振り結果（doom1.wad 配置後）**: `linuxxdoom` は
`W_Init`（`adding ./doom1.wad`）→ `R_Init`（textures/flats/sprites/colormaps）→
`P_Init` → `I_Init` → sfx ロード → `D_CheckNetGame` → `S_Init` → `HU_Init` →
`ST_Init` まで完走し、`I_InitGraphics` の `XOpenDisplay` で
**"Couldn't connect to display!"**（＝ホストに X が無いため）。
エンジン初期化は全て通り、残るは X サーバ（P2〜）だけであることを確認。

---

## 6. リスクと代替

- **llvmpipe JIT が W^X で詰まる** → `GALLIUM_DRIVER=softpipe`（JIT 不要）に envp で退避。激遅。
- **Xorg が logind/seat を要求** → `xserver-xorg-legacy` 同梱 + `-keeptty -novtswitch`、
  単一プロセス＝root 相当で回避。だめなら `Xorg.wrap` を介さず直接起動（済）。
- **`/tmp` FS が重い** → AF_UNIX 抽象アドレス対応（M1 代替）で socket 実体を不要に。
- **modesetting が DRI3/present を要求** → `xorg.conf` で `PageFlip"false"` `Atomic"false"`
  `AccelMethod"none"` `ShadowFB"true"`、GLX は `+iglx`（indirect）。それでも不可なら
  Xorg 側は `fbdev` DDX ＋ カーネル `/dev/fb0`（`FBIOGET_VSCREENINFO`/`mmap`）に切替
  （M3 を fbdev ioctl セットに縮小できるが GLX 経路は別途 `kms_swrast` 用に card0 が要る）。
