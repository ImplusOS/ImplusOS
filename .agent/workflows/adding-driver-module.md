# Workflow: Adding a Driver Module

ImplusOS に新しいローダブルドライバモジュールを追加する手順。

## Overview

ドライバモジュールは **PIC (Position-Independent Code) の ELF 共有オブジェクト** としてビルドされ、UEFI ブートローダーによって起動時にメモリにプリロードされる。

```
Kernel/Drivers/DrvMain/Server/<Category>/<DriverName>/  ← 1. ディレクトリ作成
  ├── <DriverName>.c                                     ← 2. ドライバ実装
  └── Makefile                                           ← 3. ビルド定義
Kernel/Drivers/Module/DriverManager.c                    ← 4. モジュール ID 登録
Kernel/Drivers/Module/DriverModule.h                     ← 5. ID 定義
Docs/Architecture/Driver_Module_Guide.md                 ← 6. ドキュメント更新
```

## Step 1: ディレクトリ作成

```bash
mkdir -p Kernel/Drivers/DrvMain/Server/<Category>/<DriverName>
```

カテゴリ例: `Display/`, `NIC/`, `Storage/`, `Input/`

## Step 2: ドライバモジュールを実装

エントリポイントは `driver_module_init()` で、カーネルから `driver_kernel_api_t *` を受け取る。

```c
#include "Kernel/Drivers/Module/DriverModule.h"

static driver_kernel_api_t *g_api = NULL;

// エントリポイント (リンカスクリプトで -e driver_module_init 指定)
void *driver_module_init(driver_kernel_api_t *api) {
    g_api = api;

    // 初期化処理
    // ...

    // ドライバの vtable ポインタを返す (または NULL)
    return &my_driver_vtable;
}
```

### カーネル API の使用

`driver_kernel_api_t` 経由で以下の機能にアクセス可能:

| カテゴリ | 関数例 |
|---|---|
| タイマー | `api->timer_msleep()`, `api->timer_hz()`, `api->timer_ticks()` |
| メモリ | `api->malloc()`, `api->free()`, `api->dma_alloc()`, `api->dma_free()` |
| メモリ操作 | `api->memset()`, `api->memcpy()` |
| ポート I/O | `api->inb()`, `api->outb()`, `api->inl()`, `api->outl()` |
| ディスク | `api->disk_read()`, `api->disk_write()` |
| PCI | `api->pci_read_config()`, `api->pci_write_config()` |
| MMIO | `api->map_mmio_virt()` |
| デバッグ | `api->serial_write_char()`, `api->serial_write_string()` |

## Step 3: Makefile を作成

`Kernel/Drivers/module.mk` のパターンに従う:

```makefile
include ../../../module.mk

MODULE_NAME := MyDriver

SRCS := MyDriver.c

OBJS := $(SRCS:.c=.o)

all: $(MODULE_NAME).ELF

%.o: %.c
	$(CC) $(MODULE_CFLAGS) -c $< -o $@

$(MODULE_NAME).ELF: $(OBJS)
	$(LD) $(MODULE_LDFLAGS) $^ -o $@

clean:
	rm -f $(OBJS) $(MODULE_NAME).ELF
```

### ビルドフラグ (module.mk で定義)

```
CFLAGS:  -fPIC -DIMPLUS_DRIVER_MODULE -DKERNEL -ffreestanding -fno-stack-protector
         -mcmodel=large -mno-red-zone -nostdlib -Wall -Wextra
LDFLAGS: -shared -Bsymbolic -e driver_module_init -z max-page-size=4096
         --build-id=none -nostdlib
```

## Step 4: モジュール ID を登録

`Kernel/Drivers/Module/DriverModule.h` に新しい ID を追加:

```c
#define DRIVER_MODULE_ID_MY_DRIVER  7  // 未使用の ID を選択
```

`Kernel/Drivers/Module/DriverManager.c` で ID とモジュールを紐付ける。

## Step 5: ビルド確認

```bash
make clean && make
```

- ドライバ ELF が `Build/Modules/` 以下に生成される
- `make driver_stage` で `Build/Kernel/Drivers/` にコピーされる
- `make image_esp` で ISO に含まれる

## Step 6: ドキュメント更新

`Docs/Architecture/Driver_Module_Guide.md` に新しいドライバの情報を追加する。

## Module Lifecycle (参考)

```
UEFI Bootloader
  └── ESP:/Kernel/Driver/*.ELF をメモリにロード
       └── BOOT_INFO.LoadedFiles[] に格納

Kernel Boot
  └── driver_module_manager_init()
       └── 各 ELF blob をカーネルヒープにコピー・登録

Runtime
  └── driver_module_manager_load(MODULE_ID)
       ├── ELF リロケーション (R_X86_64_RELATIVE)
       ├── driver_module_init() 呼び出し
       └── vtable ポインタを取得
```

## Checklist

- [ ] `Server/<Category>/<DriverName>/` ディレクトリを作成
- [ ] ドライバ本体を実装 (`driver_module_init()` エントリポイント)
- [ ] Makefile を `module.mk` パターンに従って作成
- [ ] `DriverModule.h` にモジュール ID を追加
- [ ] `DriverManager.c` に登録
- [ ] `make clean && make` でビルド成功を確認
- [ ] `make image_esp && make run_usb` で動作確認
- [ ] `Driver_Module_Guide.md` を更新
