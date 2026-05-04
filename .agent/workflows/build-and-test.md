# Workflow: Build & Test

ImplusOS のビルドからテスト実行までの手順。

## Prerequisites

ビルド環境のセットアップ (Ubuntu / Debian 系):

```bash
sudo apt install -y build-essential pkg-config git make cmake
sudo apt install -y gcc-multilib g++-multilib
sudo apt install -y nasm binutils
sudo apt install -y gcc-x86-64-elf g++-x86-64-elf  # クロスコンパイラ
sudo apt install -y gnu-efi
sudo apt install -y parted qemu-system-x86 gdb
sudo apt install -y dosfstools xorriso mtools util-linux
```

## Build

### フルビルド

```bash
make
```

これにより以下がビルドされる:
- UEFI ブートローダー (`Build/Loader/BOOTX64.EFI`)
- カーネル (`Build/Kernel/Kernel_Main.ELF`)
- ユーザーランド Init (`Build/Userland/Userland.ELF`)
- ドライバモジュール群
- ユーザーランドアプリケーション群

### クリーンビルド

```bash
make clean && make
```

### ISO イメージ作成

```bash
make image_esp
```

`Image/ImplusOS.iso` が生成される。

## Run (QEMU)

### USB XHCI ブート (推奨)

```bash
make run_usb
```

### IDE ブート

```bash
make run_ide
```

### QEMU 設定

- CPU: 4 コア (SMP)、KVM 有効
- メモリ: 4 GB
- デバイス: USB XHCI (キーボード / マウス)、VirtIO-Net、NVMe
- ファームウェア: OVMF (UEFI)
- シリアル: stdio に出力 (カーネルログ確認用)

## Debug

### シリアルログ確認

QEMU 起動後、ターミナルにカーネルのシリアル出力 (COM1, 115200 baud) が表示される。

### GDB デバッグ

1. QEMU を `-s -S` オプション付きで起動 (Makefile の QEMU_COMMON に追加):
   ```bash
   qemu-system-x86_64 [options] -s -S
   ```

2. 別ターミナルで GDB を接続:
   ```bash
   gdb Build/Kernel/Kernel_Main.ELF
   (gdb) target remote :1234
   (gdb) break kernel_main
   (gdb) continue
   ```

### Doxygen ドキュメント生成

```bash
doxygen Doxyfile
```

## Troubleshooting

| 問題 | 対処 |
|---|---|
| `x86_64-elf-gcc: not found` | クロスコンパイラがインストールされていない → Prerequisites の手順を確認 |
| OVMF ファイルが見つからない | `OVMF_CODE_4M.fd`, `OVMF_VARS_4M.fd` がリポジトリ直下にあることを確認 |
| `make image_esp` で権限エラー | `sudo` が必要な操作がある (losetup, mount) → sudo 権限で実行 |
| QEMU で起動しない | KVM が有効か確認 (`ls /dev/kvm`) → KVM なしで動作させる場合は `-enable-kvm` を削除 |
