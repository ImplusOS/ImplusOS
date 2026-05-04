# Coding Standards

ImplusOS のコードベース全体に適用されるコーディング規約。

## Language

| コンポーネント | 言語 |
|---|---|
| Kernel / Userland | C (C11 相当) |
| アセンブリ | NASM (Intel syntax, elf64) |
| ビルドシステム | GNU Make |

## Indentation & Formatting

- **C ソースコード**: スペース 4 つ。タブは使用しない。
- **Makefile**: インデントにはタブを使用 (Make の仕様)。
- **1 行の長さ**: 100 文字以内を推奨。超える場合は適切に改行する。
- **ブレース**: K&R スタイル (開き括弧は同じ行)。

```c
if (condition) {
    do_something();
} else {
    do_other();
}
```

## Naming Conventions

| 対象 | 規則 | 例 |
|---|---|---|
| 関数 | `snake_case` — サブシステムプレフィックス付き | `vfs_read_file()`, `process_create()`, `ipc_send_message()` |
| 型 (typedef) | `snake_case_t` | `os_status_t`, `display_driver_t`, `fat32_bpb_t` |
| 構造体 (タグ) | `UPPER_SNAKE_CASE` (タグ名) または typedef 時は `snake_case_t` | `BOOT_INFO`, `os_status_t` |
| マクロ / 定数 | `UPPER_SNAKE_CASE` — プレフィックス付き | `OS_CONFIG_PROCESS_MAX_COUNT`, `SYSCALL_FILE_OPEN` |
| Enum 値 | `UPPER_SNAKE_CASE` | `OS_STATUS_OK`, `OS_STATUS_NOT_FOUND` |
| ファイル名 | `PascalCase` または `PascalCase_PascalCase` | `Kernel_Main.c`, `Memory_Main.c`, `VFS.c` |
| ディレクトリ名 | `PascalCase` | `ProcessManager/`, `WindowManager/`, `Drivers/` |

## Header Guards

- `#pragma once` を使用する。`#ifndef` / `#define` ガードは使用しない。

```c
#pragma once

// header contents
```

## Comments

- **言語**: 英語で記述する。
- **スタイル**: `//` (行コメント) および `/* */` (ブロックコメント)。
- **Doxygen**: パブリック API には Doxygen 形式のコメントを付与する (`Doxyfile` で生成可能)。

## Return Values & Error Handling

- カーネルサブシステムの関数は **`os_status_t`** (`int64_t`) を返す。
  - 成功: `OS_STATUS_OK` (0)
  - エラー: 負の値 (`OS_STATUS_NOT_FOUND`, `OS_STATUS_INVALID_ARG` 等)
- エラーチェックには `os_status_is_error()` を使用する。
- 詳細は `Kernel/Common/Status.h` および `Docs/Architecture/Status_Codes.md` を参照。

```c
os_status_t result = vfs_read_file(path, buffer, size);
if (os_status_is_error(result)) {
    // handle error
}
```

## Compiler Warning Flags

以下の警告フラグを維持し、警告ゼロを目指す:

```
-Wall -Wextra -Wtype-limits -Wconversion -Wsign-conversion -Wshadow
```

- 新規コードで警告が出ないことを確認してからコミットする。
- `-Wconversion` / `-Wsign-conversion` に対応するため、明示的なキャストを使用する。

## Include Order

1. 自ファイルに対応するヘッダ (例: `Foo.c` → `Foo.h`)
2. カーネル / プロジェクト内ヘッダ
3. libc ヘッダ (`<string.h>`, `<stdint.h>` 等)

```c
#include "VFS.h"                    // 1. 対応ヘッダ
#include "Kernel/Common/Status.h"   // 2. プロジェクト内
#include <string.h>                 // 3. libc
#include <stdint.h>
```
