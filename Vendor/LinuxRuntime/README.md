# Vendor/LinuxRuntime — 外来 Linux バイナリ用ランタイム同梱

ImplusOS が **外来の動的リンク Linux ELF**（当面は
`Userland/Application/Chromium/Resource/chrome`）を起動できるように、
Linux の動的リンカ・共有ライブラリ閉包・ロケールを **ビルド済みバイナリのまま**
（無改変で）OS イメージへ取り込むための機構。

設計の背景と全体計画は [`../../Docs/Others/TODO_glibc_Port.md`](../../Docs/Others/TODO_glibc_Port.md)。

## 方針（TODO_glibc_Port.md §2 より）

- **第三者バイナリは無改変**。`.deb` から取り出した `.so` をそのままステージする。
  パッチ・`patchelf`・リンク時差し替えは一切しない。
- **入手元は Debian の公式アーカイブ**（`snapshot.debian.org` でスナップショット日を
  固定し、`.deb` ごとに sha256 をピン）。`chrome` が要求する glibc シンボルは
  最大 `GLIBC_2.25`（`readelf -V` 実測）なので、Debian trixie（glibc 2.41 系）で
  十分に満たされる。
- **実バイナリはリポジトリにコミットしない**（`cache/` は `.gitignore`）。
  コミットするのはテキストのピン（`packages.lock`, `closure.txt`）のみ。
  初回ビルドだけネットワークが要り、以後は sha256 一致で再現ビルドできる。
  → 実バイナリの同梱を望む場合は `TODO_glibc_Port.md` §7-6 を参照。
- ビルドルールはこの Makefile に閉じる。トップ `Makefile` からは
  `linux_runtime_stage` で `-C` 委譲するだけ（`Vendor/Library` と同じ構造）。

## ファイル

| ファイル | 役割 |
|---|---|
| `Makefile` | `resolve` / `fetch` / `stage` / `locale` / `verify` / `clean` |
| `resolve.sh` | `Resource/chrome` の `DT_NEEDED` 閉包を Debian の `Contents`/`Packages` から解決し `packages.lock` と `closure.txt` を生成（開発時のみ） |
| `fetch.sh` | `packages.lock` の `.deb` を `cache/` へ取得し sha256 照合 |
| `stage.sh` | `cache/*.deb` を展開し `<STAGE>/lib64`・`<STAGE>/usr/lib/...` へ実体コピー（symlink 解決）、`LICENSES/` に copyright 収集、`closure.txt` の全 soname が揃うか自己検査 |
| `packages.seed.txt` | 解決の起点にする Debian パッケージ名（`chrome` の直接依存に対応） |
| `packages.lock` | 生成物・**コミット対象**。`pkg<TAB>version<TAB>deb-url<TAB>sha256` |
| `closure.txt` | 生成物・**コミット対象**。`soname<TAB>pkg<TAB>staged-path` |
| `cache/` | 取得した `.deb`（`.gitignore`） |
| `LICENSES/` | 各パッケージの `copyright`（`stage.sh` が収集、`Docs/OSS_License/` へ配置） |

## 使い方

```bash
# 閉包の再解決（chrome を差し替えたときなど。ネットワーク必須・重い）
make -C Vendor/LinuxRuntime resolve

# ピン済み .deb の取得（初回のみネットワーク）
make -C Vendor/LinuxRuntime fetch

# 任意ディレクトリへステージ（イメージビルドが呼ぶ）
make -C Vendor/LinuxRuntime stage STAGE_DIR=/path/to/payload_root

# C.UTF-8 ロケール生成
make -C Vendor/LinuxRuntime locale STAGE_DIR=/path/to/payload_root

# 取得物の sha256 と閉包の充足を検査
make -C Vendor/LinuxRuntime verify
```

## Mesa / VirtIO-GPU virgl

The Xorg runtime staging step also copies Mesa's DRI modules from the pinned
Debian `libgl1-mesa-dri` package, including `virtio_gpu_dri.so`, when present.
The ordinary ELF dependency closure supplies `libdrm.so.2`; `stage-xorg.sh`
checks both pieces and emits a warning if the virgl DRI module or libdrm is
missing. `swrast_dri.so` and `kms_swrast_dri.so` remain staged for software
rendering fallback.

At runtime, Mesa discovers `/dev/dri/renderD128` through libdrm. The
kernel publishes it as a character device (DRM major 226, render minor 128),
reports `virtio_gpu` through `DRM_IOCTL_VERSION`, and provides the matching
VirtIO PCI identity at `/sys/dev/char/226:128`. The primary node
`/dev/dri/card0` remains the Xorg scanout/KMS device. The Xorg log already
shows the DRI3 extension being initialized. The modesetting DDX's supported
`PageFlip` option selects DRI3 page flipping and defaults to on; this runtime
keeps it enabled. The old `Option "DRI3" "true"` entry was not a valid
modesetting-driver option and has been removed. The separate
`[DRI2] ... kms_swrast` AIGLX provider line in the Xorg log alone does not
prove which renderer a DRI3 client receives. DRI3 clients need libdrm to map
the KMS fd to the associated render node; the kernel now publishes the
`/sys/dev/char/226:0/device/drm` and `/sys/dev/char/226:128/device/drm`
entries as well as the node's PCI identity. When the render node is handed to
Mesa, its `virtio_gpu` name selects `virtio_gpu_dri.so`. An Intel display
adapter can still own scanout while a VirtIO GPU provides virgl rendering.

The host-side QEMU device must expose the virgl backend and a GL-capable
display. For a GTK display, use:

```sh
-display gtk,gl=on -device virtio-gpu-gl-pci
```

Do not use the plain `virtio-gpu` 2D device for this test. The boot log's
`[vgpu] features` line is the quickest check: device feature bit 0
(`VIRGL`) must be set in `dev0`, and bit 0 must appear in
`negotiated`. If `negotiated` is only `0x2`, the guest has EDID but no
virgl; Mesa will correctly fall back to software rendering.

Check that the node and the libdrm sysfs identity are both visible:

```sh
ls -l /dev/dri
stat -c '%F %t:%T %n' /dev/dri/renderD128
cat /sys/dev/char/226:128/uevent
cat /sys/dev/char/226:128/device/uevent
```

Then verify Mesa's selected driver and renderer:

```sh
LIBGL_DEBUG=verbose MESA_LOADER_DRIVER_OVERRIDE=virtio_gpu glxinfo -B
```

The override is diagnostic only: do not set it globally on systems without a
ready VirtIO virgl device. A successful Mesa DRI module load is not itself proof
that GPU command submission works; verify that the renderer is virgl rather
than `llvmpipe`, and test an actual GL workload. Chromium may independently
disable GPU acceleration based on its build, sandbox, or GPU blocklist policy.

The kernel implements the legacy VirtIO-GPU DRM path used by Mesa's virgl
OpenGL winsys. Blob resources, syncobj/sync_file fences, Venus/Vulkan, and
gfxstream are not provided by this implementation. See
[`Kernel/GPU_ACCELERATION.md`](../../Kernel/GPU_ACCELERATION.md) for the
kernel ABI subset and validation plan.

### Shell / BusyBox startup diagnostics

A message such as `[execve-fail] path=/bin/busybox elf_err=paging map failed`
with exit status 127 is not evidence that the `ls` or `rm` applets are missing.
It means the kernel failed while mapping an ELF load segment into the process
address space. The kernel now caches each process's display-framebuffer mapping
instead of allocating a new virtual range on every redraw, and emits
`[elfmap]` / `[pagemap]` diagnostics with the failing address range and
remaining free-page count. Rebuild the kernel and image before using these
messages to diagnose a new boot; older images can still exhibit the repeated
framebuffer mappings shown in earlier logs.

The current statically linked BusyBox is an `ET_EXEC` image linked at
`0x400000`. That address overlaps the kernel's low identity map. The kernel
intentionally refuses to replace an already-owned identity frame with an
unrelated physical frame, because doing so would make kernel physical pointers
resolve to different memory while the process address space is active. A
failure such as `[pagemap] identity frame busy ... vaddr=0x400000` is therefore
an address-layout conflict, not physical-memory exhaustion and not a missing
BusyBox applet. Do not remove the identity-map safety check to work around it.

The durable userspace-side direction is to rebuild BusyBox as PIE
(`ET_DYN`, e.g. static PIE) and verify its ELF type before staging it:

```sh
readelf -h Userland/Application/BusyBox/Resource/busybox | grep 'Type:'
```

The expected type is `DYN`, not `EXEC`. This still needs runtime validation
with the ImplusOS ELF loader, including any relocations used by the static PIE.

## ライセンス

同梱する各ライブラリは動的リンク・無改変。glibc は LGPL-2.1+、X client
ライブラリ群は MIT/X11、NSS は MPL-2.0、GLib/GTK 系は LGPL-2.1+、ALSA lib は
LGPL-2.1+ 等。`stage.sh` が各 `.deb` の
`usr/share/doc/<pkg>/copyright` を `LICENSES/` に集め、`Docs/OSS_License/LinuxRuntime/`
へ配置する。入手元（`packages.lock` の URL）と全文同梱により LGPL の再配布条件を
満たす。Chromium バイナリ自体の再配布条件は本ディレクトリの対象外
（`Userland/Application/Chromium/` 側で扱う）。
