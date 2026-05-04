# Architecture Constraints

ImplusOS の技術的制約とアーキテクチャ上の前提条件。

## Target Platform

| 項目 | 値 |
|---|---|
| CPU アーキテクチャ | x86-64 (Long Mode) |
| ブート方式 | UEFI (`gnu-efi` 経由) |
| カーネルモデル | モノリシック + ローダブルドライバモジュール |
| 検証環境 | QEMU + OVMF (物理ハードウェアは非保証) |

## Freestanding Environment

ImplusOS はフリースタンディング環境で動作する。**ホスト OS の標準ライブラリは一切使用できない。**

- コンパイルフラグ: `-ffreestanding -nostdlib -nostartfiles -nodefaultlibs`
- 使用可能な libc: `libc/` ディレクトリ配下の自前実装のみ
  - `string.h` (memcpy, memset, strlen, strcmp 等)
  - `stdlib.h` (malloc, free, calloc, realloc, atoi 等)
  - `stdio.h` (printf — カーネル内のみ)
  - `math.h`
  - `errno.h`
  - `assert.h`
- **禁止事項**: `<stdlib.h>` や `<stdio.h>` 等のホスト側ヘッダを直接使用してはならない。

## Cross Compiler

| ツール | コマンド |
|---|---|
| C コンパイラ | `x86_64-elf-gcc` |
| C++ コンパイラ | `x86_64-elf-g++` |
| リンカ | `x86_64-elf-ld` |
| アセンブラ | `nasm` |

**ホストの gcc / ld を使用してはならない。** 必ずクロスコンパイラを使用する。

## Memory Model & ABI

| コンポーネント | メモリモデル | PIC/PIE |
|---|---|---|
| カーネル | `-mcmodel=large` | `-fPIE` |
| ドライバモジュール | `-mcmodel=large` | `-fPIC` (共有ライブラリ形式) |
| ユーザーランド | `-mcmodel=large` | `-fno-pic` |

- **Red zone**: 全コンポーネントで禁止 (`-mno-red-zone`) — 割り込みハンドラの安全性のため。
- **スタック保護**: 無効 (`-fno-stack-protector`) — フリースタンディング環境ではランタイムサポートがない。

## Paging & Address Space

### 4-Level Page Tables

- `PML4 → PDPT → PD → PT`
- NX (No-Execute) ビット: `IA32_EFER` で有効化済み

### カーネルアドレス空間

- 低メモリ: アイデンティティマッピング
- カーネル `.text`: `0x100000` (1 MiB) から開始 (リンカスクリプト `Kernel_Main.ld`)

### ユーザーアドレス空間

| 領域 | アドレス範囲 | 用途 |
|---|---|---|
| コード | `0x4000000000` – `0x4100000000` | ELF ロードセグメント |
| ヒープ | `0x4100000000` – `0x47E0000000` | ユーザーヒープ (上方伸長) |
| スタック | `0x47E0000000` – `0x4800000000` | ユーザースタック (32 MiB, 下方伸長) |

## Syscall ABI

- AMD64 `SYSCALL` / `SYSRET` 命令を使用
- MSR (STAR, LSTAR, SFMASK) で設定
- システムコール番号の定義: `Kernel/Syscall/Syscall_Main.h`

## Third-Party Dependencies

外部ライブラリは `Thirdparty/` ディレクトリに格納されたもののみ使用可。

| ライブラリ | 用途 |
|---|---|
| `stb_image.h` | 画像デコード |
| `stb_truetype.h` | フォントレンダリング |

新たな外部依存を追加する場合は、`Thirdparty/` に配置し、ライセンスを `Thirdparty/LICENSEFILE/` に追加すること。

## Linker Scripts

| ファイル | 対象 | ベースアドレス |
|---|---|---|
| `Kernel/Kernel_Main.ld` | カーネル ELF | `0x100000` |
| `Userland/Userland.ld` | ユーザーランド Init | `0x4000000000` |

リンカスクリプトを変更する場合は、ページングのマッピングとの整合性を必ず確認すること。
