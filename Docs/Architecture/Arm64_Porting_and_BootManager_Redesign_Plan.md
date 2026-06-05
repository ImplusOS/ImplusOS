# Arm64移植およびBootManager再設計計画書 — ImplusOS

## 1. 概要 (Overview)

本ドキュメントは、x86-64およびUEFI/BIOSハイブリッド環境で動作するImplusOSを、`arm64` (AArch64) アーキテクチャに移植し、かつBootManagerを特定のアーキテクチャやSDK（GNU-EFI等）に依存しない独立したモジュールとして再設計するための計画書です。

### 1.1 背景と課題
現在、ImplusOSのBootManagerは、UEFI版 (`BootManager/UEFI/`) とBIOS版 (`BootManager/BIOS/`) でソースコードが完全に分かれています。これには以下の問題があります。
- **コードの重複**: ELFのパースや描画処理、メモリ情報・モジュール前処理などのロジックが双方に重複して存在し、片方の修正がもう片方に反映されにくい。
- **プラットフォーム依存**: UEFI版はGNU-EFIの型・API（`EFI_SYSTEM_TABLE` や `uefi_call_wrapper`）に依存し、BIOS版はx86のリアルモード・プロテクトモード遷移やBIOS割り込み（Int 13hなど）に依存しているため、そのままでは第3のアーキテクチャ（arm64）に対応できません。

### 1.2 設計目標
1. **BootManagerの共通化**: アーキテクチャやFirmware SDKに依存しない「コア論理」と、各プラットフォーム固有の処理を行う「プラットフォーム抽象化レイヤー (PAL: Platform Abstraction Layer)」に分割します。
2. **arm64への対応**: UEFIベースのAArch64ブート環境に対応させ、QEMU virtマシン上で動作可能なカーネルおよびブートストラップを設計します。

---

## 2. BootManagerの共通化設計 (BootManager Redesign)

### 2.1 レイヤー構造

共通化後のブートシーケンスにおけるコンポーネント構造は以下の通りです。

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Core BootManager (共通コード)                     │
│  - ELF Loader & Relocation (x86_64 / arm64)                            │
│  - UI Manager (stb_truetype, BMP / Framebuffer Blending)               │
│  - Driver Preloader & OS Boot Logic                                   │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ PAL Interface (API VTable)
┌───────────────────────────────────▼────────────────────────────────────┐
│                    Platform Abstraction Layer (PAL)                    │
├─────────────────────────┬─────────────────────────┬────────────────────┤
│      UEFI x86_64        │       UEFI arm64        │     BIOS x86_32    │
│    (BOOTX64.EFI)        │     (BOOTAA64.EFI)      │    (BootManager)   │
└─────────────────────────┴─────────────────────────┴────────────────────┘
```

### 2.2 PAL (Platform Abstraction Layer) インターフェース定義

Core BootManagerがプラットフォーム側に要求するAPIセットを構造体 `boot_pal_t` として定義します。

```c
#pragma once
#include <stdint.h>
#include <stddef.h>

// メモリマップ記述子（プラットフォーム非依存表現）
typedef struct {
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint32_t type;
    uint32_t attribute;
} pal_memory_descriptor_t;

typedef struct {
    // 1. メモリ管理 API
    void *(*malloc)(size_t size);
    void (*free)(void *ptr);
    void *(*alloc_pages)(size_t pages, uint64_t *phys_out);
    void (*free_pages)(void *ptr, size_t pages);

    // 2. ファイルシステム / ブロックI/O API
    int (*file_open)(const char *path, void **file_handle);
    int (*file_read)(void *file_handle, uint64_t offset, size_t size, void *buffer, size_t *bytes_read);
    void (*file_close)(void *file_handle);
    int (*file_get_size)(void *file_handle, uint64_t *size_out);
    int (*disk_read_sectors)(uint64_t lba, uint32_t count, void *buffer);

    // 3. グラフィックス API
    int (*graphics_get_framebuffer)(uint64_t *fb_base, uint64_t *fb_size, uint32_t *width, uint32_t *height, uint32_t *pitch);
    void (*graphics_present)(void); // ダブルバッファ等のフラッシュ要求

    // 4. システム情報 API
    uint64_t (*get_acpi_rsdp)(void);
    int (*get_smbios_info)(char *cpu_name, size_t cpu_max, char *manufacturer, size_t man_max, char *product_name, size_t prod_max);
    int (*get_memory_map)(pal_memory_descriptor_t *map_buffer, size_t *map_size, uint64_t *map_key);

    // 5. カーネル起動制御
    void (*enter_kernel)(uint64_t entry_point, void *boot_info);
} boot_pal_t;
```

### 2.3 Core BootManagerの責務と実装方針
- **ELFパースとローダー**: ELF64のセグメント (`PT_LOAD`) 解析を共通論理として実装します。位置独立実行形式 (PIE/Dynamic) の再配置 (Relocation) 処理については、アーキテクチャ依存部を関数ポインタや条件コンパイルで分離します。
  - x86_64: `R_X86_64_RELATIVE` (再配置値にベースアドレスを加算)
  - arm64: `R_AARCH64_RELATIVE` (再配置値にベースアドレスを加算)
- **UI描画システム**: `stb_truetype` を用いた文字ラスタライズおよびBMP展開ロジックは、PALから取得したリニアフレームバッファ (`graphics_get_framebuffer`) に直接ピクセルを書き込む方式で完全に共通化します。
- **モジュール前読み込み**: カーネルイメージファイルや各種ドライバELFファイル (`Kernel/Driver/*.ELF`)、フォントデータをファイルシステムAPI経由でメモリにロードし、共通の `BOOT_INFO` 構造体を構築します。

---

## 3. カーネルのarm64移植計画 (Kernel arm64 Porting Plan)

arm64 (AArch64) アーキテクチャの移植における具体的な設計項目です。

### 3.1 ブートフローと初期化シーケンス

```
[BOOTAA64.EFI (PAL)] ─► [Core BootManager] ─► [kernel_main (Kernel)]
  - UEFI初期化             - Kernel/Driverロード    - EL1/SP_EL1の確認
  - GOP / メモリマップ取得   - 再配置処理             - MMUの初期化 (TTBR0/1)
  - ExitBootServices      - 制御渡し               - GIC初期化
                                                   - PSCIによるSMP起動
```

### 3.2 特異レジスタとCPU初期設定 (EL1遷移)
UEFIは通常、AArch64の **EL2** (Hypervisor) または **EL1** (Kernel) でブートローダーを起動します。カーネルは通常EL1で動作するため、ブートストラップコード内で例外レベルを確認し、必要に応じてEL2からEL1に降下します。
- **SP_EL1へのスタック設定**: カーネル用のスタックポインタを設定します。
- **SPSR_EL2 / ELR_EL2の設定**: EL1に安全に降下するために `eret` を使用します。
- **システム制御レジスタの初期化**: `CPACR_EL1` を設定して浮動小数点演算 (FP/SIMD) を有効化します。

### 3.3 仮想メモリ (4-Level Paging)
AArch64は、x86-64と類似した4段階のページングテーブル構造（L0 -> L1 -> L2 -> L3）を持っています。
- **変換テーブルレジスタ (TTBR0_EL1 / TTBR1_EL1)**:
  - `TTBR0_EL1` は下位アドレス空間（ユーザー空間 `0x0000_0000_0000_0000` -）の変換ベース。
  - `TTBR1_EL1` は上位アドレス空間（カーネル空間 `0xFFFF_0000_0000_0000` -）の変換ベース。
- **TCR_EL1 (Translation Control Register)**:
  - ページサイズとして **4 KiB** を選択 (`TG0=0b00`, `TG1=0b10`)。
  - アドレス幅を **48ビット** に設定 (`TxSZ=16`)。
- **MAIR_EL1 (Memory Attribute Indirection Register)**:
  - メモリアトリビュートを以下のように定義し、テーブルエントリーのインデックスに指定します。
    - Index 0: デバイスメモリ (Device-nGnRE, MMIO用)
    - Index 1: 通常メモリ (Normal Memory, インナー/アウター Write-Backキャッシュ有効)

### 3.4 割り込み管理 (ARM GIC v2/v3)
x86-64の APIC / IOAPIC に代わり、AArch64標準の **GIC (Generic Interrupt Controller)** を使用します。
- **GIC Distributor (GICD)**: 全CPUコアへの割り込み配信設定（有効化、優先度設定、CPUアフィニティ）。
- **GIC CPU Interface (GICC) / Redistributor (GICR)**: 自コアにおける割り込みのハンドリング、EOI (End of Interrupt) の送信。
- タイマー割り込みとして、ARM標準の **Generic Timer**（CPU内蔵タイマー, `CNTP_TVAL_EL0` レジスタ）を使用します。

### 3.5 マルチコア起動 (SMP)
x86-64の LAPIC を使った INIT-SIPI 送信に代わり、arm64では標準化されたファームウェアインターフェースである **PSCI (Power State Coordination Interface)** を利用します。
- **PSCI_CPU_ON 呼び出し**:
  - `SMC` (Secure Monitor Call) または `HVC` (Hypervisor Call) 命令を使用。
  - 第1引数に `PSCI_CPU_ON` のファンクションID、第2引数に対象CPUの MPIDR アフィニティ、第3引数にセカンダリCPUのエントリアドレス、第4引数にコンテキストIDを渡します。
  - 起動されたセカンダリコアは、`TTBRx` および `TCR_EL1` を適用して仮想メモリを有効化し、カーネルのスケジューラに参加します。

### 3.6 例外ベクターテーブル (Exception Vectors)
例外発生時のハンドラ群を配置する例外ベクターテーブルを定義し、そのアドレスを **`VBAR_EL1`** (Vector Base Address Register) に登録します。
- ベクターテーブルは **2048バイト境界** にアライメントされている必要があります。
- テーブルは以下の4つの実行コンテキスト区分（各4つの例外タイプ：Synchronous, IRQ, FIQ, SError）の合計16個のエントリから構成されます。
  1. 例外発生元と同じELでSP_EL0を使用している場合
  2. 例外発生元と同じELでSP_ELxを使用している場合（カーネル内例外）
  3. 下位のEL（EL0: ユーザー空間）からAArch64で発生した場合
  4. 下位のEL（EL0: ユーザー空間）からAArch32で発生した場合（非サポート）

### 3.7 システムコール (Syscall)
AArch64では、x86-64の `SYSCALL` に代わって **`SVC #0`** (Supervisor Call) 命令を使用します。
- **レジスタマッピング**:
  - システムコール番号: **`x8`** レジスタ
  - 引数 1〜6: **`x0` 〜 `x5`** レジスタ
  - 戻り値: **`x0`** レジスタ
- ユーザー空間から `SVC` が実行されると、EL1の例外ベクターテーブルの「下位のELからのSynchronous例外」エントリへジャンプします。カーネルは `ESR_EL1` (Exception Syndrome Register) を確認し、例外コードが `0x15` (SVC) である場合にシステムコールハンドラへディスパッチします。

### 3.8 プロセス/スレッド コンテキスト
スレッド切り替え時に保存および復元すべきレジスタ構造体 `context_t` を定義します。
- **保存対象レジスタ**:
  - 汎用レジスタ: `x19` 〜 `x29` (Callee-saved registers)
  - リンクレジスタ: `x30` (LR)
  - 例外復帰用 PC / 状態レジスタ: `ELR_EL1` (PC), `SPSR_EL1` (CPU状態)
  - スタックポインタ: `SP_EL0` (ユーザー用スタック), `SP_EL1` (カーネル用スタック)

---

## 4. ビルド・統合計画 (Build & Integration Plan)

### 4.1 ツールチェーン定義

| ツール区分 | x86_64 用 | arm64 用 |
|---|---|---|
| クロスコンパイラ | `x86_64-elf-gcc` | `aarch64-elf-gcc` |
| リンカ | `x86_64-elf-ld` | `aarch64-elf-ld` |
| アセンブラ | `nasm` | `aarch64-elf-as` / GCCインライン |
| EFIビルド用 (UEFI) | `x86_64-w64-mingw32-gcc`等 | `aarch64-elf-gcc` (clang + lld も選択可) |

### 4.2 Makefile 変更方針
`Makefile` に `ARCH=arm64` 時のビルドターゲットを追加します。

```makefile
ifeq ($(ARCH),arm64)
    CC   := aarch64-elf-gcc
    LD   := aarch64-elf-ld
    # arm64固有コンパイルオプション
    KERNEL_CFLAGS := -ffreestanding -fno-pic -mgeneral-regs-only -mstrict-align -nostdlib
    BOOTLOADER_EFI := $(BUILD_DIR)/Loader/BOOTAA64.EFI
else
    # 既存のx86_64設定
endif
```

### 4.3 UEFI arm64イメージの作成方法
UEFIの規約に則り、FAT32 ESP (EFI System Partition) イメージ上の決まった位置にバイナリを配置します。
- パス: `/EFI/BOOT/BOOTAA64.EFI`
- インストールイメージビルド時に `mtools` もしくは `hdiutil`/`parted` 等を使用し、FAT32イメージ内の `/EFI/BOOT/` ディレクトリ配下に `BOOTAA64.EFI` と `BOOTMANAGER.EFI` を書き込みます。

### 4.4 QEMUシミュレーション実行
QEMU arm64でのエミュレーション実行環境を `Makefile` の `run_uefi_arm64` ターゲットとして定義します。

```makefile
QEMU_ARM64_COMMON := \
    -machine virt,gic-version=3 \
    -cpu max \
    -smp 4 \
    -m 4G \
    -bios ./AAVMF_CODE.fd \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    -drive if=none,id=usbstick,format=raw,file=$(IMAGE) \
    -device usb-storage,bus=xhci.0,drive=usbstick \
    -serial stdio

run_uefi_arm64:
    qemu-system-aarch64 $(QEMU_ARM64_COMMON)
```

※実行には、AArch64用のUEFIファームウェアバイナリ（`AAVMF_CODE.fd` もしくは `QEMU_EFI.fd` など）が必要となります。

---

## 5. 移行ロードマップ (Migration Roadmap)

移植および共通化作業は以下のフェーズに分けて順次実行します。

### フェーズ 1: BootManagerのPAL抽象化リファクタリング (x86_64対象)
1. `BootManager/Core/` を作成し、ELFローダー・UI描画・ローディングロジックを共通化。
2. `BootManager/UEFI/` および `BootManager/BIOS/` 内に `boot_pal_t` の実装を記述し、共通 Core を呼び出す形にリファクタリング。
3. 既存の x86_64 UEFI/BIOS 環境で変更前と全く同じ動作を維持しているか回帰テストを実施。

### フェーズ 2: ツールチェーン整備および arm64 UEFI BootLoader/PAL の実装
1. AArch64向けツールチェーン (`aarch64-elf-gcc`) を用意。
2. `BootLoader/arm64/UEFI/` に `BOOTAA64.EFI` となる初期エントリーポイントと、UEFI用 `boot_pal_t` 実装を作成。

### フェーズ 3: Kernel 依存部抽象化の実装と arm64 カーネル起動
1. `Kernel/Arch/arm64/` 以下のスタブ関数群を本計画書に基づき本実装に書き換え。
2. カーネル起動初期（EL1遷移、初期ページテーブル適用、シリアルCOMポート代替（UART PL011）ドライバ接続）のデバッグを実施。

### フェーズ 4: 割り込み・マルチコア・スケジューラ統合
1. GIC v3割り込みコントローラを統合し、タイマー割り込みによるプリエンプション動作を有効化。
2. PSCIを用いたマルチコア初期化を有効化。
3. ユーザーランドタスク (`Userland/Userland.ELF`) の起動確認。
