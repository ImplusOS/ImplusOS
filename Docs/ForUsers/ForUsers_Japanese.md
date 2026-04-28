# ImplusOS — ユーザーガイド（日本語）

## クイックスタート

### 前提条件

ImplusOS は **Linux**（Debian系 またはそれに類するディストリビューション）上で
ビルドします。x86-64 クロスコンパイルツールチェーンが必要です。

### 依存パッケージのインストール

```bash
sudo apt install -y build-essential pkg-config git make cmake
sudo apt install -y gcc-multilib g++-multilib
sudo apt install -y nasm binutils gnu-efi
sudo apt install -y gcc-x86-64-elf g++-x86-64-elf  # x86_64-elf クロスツールチェーン
sudo apt install -y parted dosfstools xorriso mtools util-linux
sudo apt install -y qemu-system-x86 gdb
```

### ビルド

```bash
make
```

以下が生成されます：
- `Build/Loader/BOOTX64.EFI` — UEFI ブートアプリケーション
- `Build/Kernel/Kernel_Main.ELF` — カーネルバイナリ
- `Build/Kernel/Drivers/*.ELF` — ドライバモジュール
- `Build/Userland/*.ELF` — ユーザーランドバイナリ

### QEMU で実行

**IDE ブート**（仮想ハードディスク）：
```bash
make run_ide
```

**USB ブート**（xHCI 経由の仮想 USB ストレージ）：
```bash
make run_usb
```

どちらのコマンドも以下を行います：
1. 全コンポーネントをビルド
2. FAT32 ディスクイメージ（`Image/disk.iso`）を作成
3. OVMF UEFI ファームウェアで QEMU を起動

### シリアル出力

ブートログとデバッグ出力はシリアルポートに送信されます。デフォルトの QEMU
設定では `-serial stdio` によりターミナルにリダイレクトされます。

---

## ディスクイメージの構成

生成される ISO には以下の構造の EFI システムパーティションが含まれます：

```
/
├── EFI/
│   └── BOOT/
│       ├── BOOTX64.EFI        ← UEFI ブートローダー
│       └── Resource/          ← リソース（画像、フォントなど）
├── Kernel/
│   ├── Kernel_Main.ELF        ← カーネルバイナリ
│   └── Driver/
│       ├── PCI_Driver.ELF
│       ├── FAT32_Driver.ELF
│       ├── PS2_Driver.ELF
│       ├── VirtIO_Driver.ELF
│       ├── ImplusOS_Generic_Display_Driver.ELF
│       └── USB_Driver.ELF
└── Userland/
    ├── Userland.ELF            ← Init プロセス
    ├── SystemApps/             ← システムアプリケーション
    └── UserApps/               ← ユーザーアプリケーション
```

---

## QEMU 設定

### デフォルト設定

| 設定 | IDE ブート | USB ブート |
|---|---|---|
| マシン | `pc`（i440FX） | `pc`（i440FX） |
| CPU 数 | 4 | 4 |
| RAM | 4 GiB | 15 GiB |
| USB | xHCI コントローラ | xHCI コントローラ |
| ネットワーク | VirtIO-Net（ユーザーモード） | VirtIO-Net（ユーザーモード） |
| UEFI ファームウェア | OVMF 4M | OVMF 4M |
| 入力 | USB キーボード＋マウス | USB キーボード＋マウス |

### OVMF ファイルパス

Makefile はデフォルトで以下のパスを使用します：
```
/usr/share/OVMF/OVMF_CODE_4M.fd
```

他の場所にインストールされている場合はオーバーライドしてください：
```bash
make run_ide OVMF_CODE=/path/to/OVMF_CODE.fd
```

---

## ビルドターゲット

| ターゲット | 説明 |
|---|---|
| `make` / `make all` | 全コンポーネントをビルド |
| `make image` | ビルド + ISO イメージ作成 |
| `make run_ide` | ビルド + イメージ + QEMU 起動（IDE モード） |
| `make run_usb` | ビルド + イメージ + QEMU 起動（USB モード） |
| `make clean` | `Build/` と `Image/` ディレクトリを削除 |

---

## デバッグ

### GDB アタッチ

QEMU をデバッグモードで起動：
```bash
qemu-system-x86_64 ... -S -s
```

GDB で接続：
```bash
gdb Build/Kernel/Kernel_Main.ELF
(gdb) target remote :1234
(gdb) continue
```

### シリアルコンソール

`serial_write_*` 関数およびカーネル `printf` の出力は
シリアルポート（`-serial stdio` でターミナルにリダイレクト）に表示されます。

---

## トラブルシューティング

### ビルドエラー: `x86_64-elf-gcc not found`

`x86_64-elf` クロスコンパイラをインストールするか、
Makefile の `CC`/`CXX`/`LD` 変数を修正してください。

### QEMU エラー: `OVMF_CODE_4M.fd not found`

OVMF パッケージをインストールしてください：
```bash
sudo apt install ovmf
```

### ブート後に黒い画面が表示される

- シリアル出力でエラーメッセージを確認してください。
- ディスクイメージが正しく作成されたか確認: `ls -la Image/disk.iso`
- USB ブートが失敗する場合は IDE ブートモードを試してください。

---

## 注意事項

- このプロジェクトは**対話型 Linux 環境**（ローカルターミナル）でのビルドを前提としています。
- CI/CD やコンテナ環境などの非対話型環境では、ディスクイメージ作成に `sudo` が
  必要なため、ビルドに成功しない場合があります。
- 実機での動作は保証されていません。動作検証は QEMU + OVMF を中心に行われています。
- **新機能**: Berkeley ソケット互換 API および C++ アプリケーションのサポートが追加されました。
