# Workflow: Adding a New System Call

ImplusOS に新しいシステムコールを追加する手順。

## Overview

```
Kernel/Syscall/Syscall_Main.h      ← 1. 番号定義
Kernel/Syscall/Syscall_*.c         ← 2. ハンドラ実装
Kernel/Syscall/Syscall_Dispatch.c  ← 3. ディスパッチ登録
Userland/Syscalls.c                ← 4. ユーザーランドラッパー
Userland/Syscalls.h                ← 5. プロトタイプ宣言
Userland/API/*.h                   ← 6. 高レベル API (任意)
Docs/Architecture/Syscall_Reference.md ← 7. ドキュメント更新
```

## Step 1: システムコール番号を定義

`Kernel/Syscall/Syscall_Main.h` に新しいシステムコール番号を追加する。

```c
#define SYSCALL_MY_NEW_CALL  200  // 未使用の番号を選択
```

**注意:**
- 既存の番号と重複しないこと
- カテゴリごとに番号帯が分かれている (参照: `Docs/Architecture/Syscall_Reference.md`)

## Step 2: ハンドラ関数を実装

既存の `Syscall_*.c` ファイルに追加するか、新しいファイル `Syscall_MyFeature.c` を作成する。

```c
#include "Syscall_Main.h"
#include "Kernel/Common/Status.h"

os_status_t syscall_my_new_call(uint64_t arg1, uint64_t arg2) {
    // 1. 引数のバリデーション
    if (arg1 == 0) {
        return OS_STATUS_INVALID_ARG;
    }

    // 2. ケーパビリティチェック (必要に応じて)
    // process_check_capability(PROCESS_CAP_XXX);

    // 3. 実処理

    return OS_STATUS_OK;
}
```

**新規ファイルを作成した場合:**
- `Kernel/Syscall/Syscall_MyFeature.h` にプロトタイプ宣言を追加
- トップレベル `Makefile` の `KERNEL_C_SRCS` にファイルパスを追加

## Step 3: ディスパッチテーブルに登録

`Kernel/Syscall/Syscall_Dispatch.c` の `syscall_dispatch()` 関数内に case を追加する。

```c
case SYSCALL_MY_NEW_CALL:
    result = syscall_my_new_call(arg1, arg2);
    break;
```

## Step 4: ユーザーランドラッパーを追加

`Userland/Syscalls.c` にユーザーランドから呼び出すラッパー関数を追加する。

```c
int64_t my_new_call(uint64_t arg1, uint64_t arg2) {
    return syscall2(SYSCALL_MY_NEW_CALL, arg1, arg2);
}
```

`syscall0` 〜 `syscall6` のヘルパーが引数の数に応じて使用可能。

## Step 5: プロトタイプ宣言

`Userland/Syscalls.h` にプロトタイプを追加する。

```c
int64_t my_new_call(uint64_t arg1, uint64_t arg2);
```

## Step 6: 高レベル API (任意)

複雑な機能の場合、`Userland/API/` にハイレベルな API ヘッダを作成する。

## Step 7: ドキュメント更新

`Docs/Architecture/Syscall_Reference.md` に新しいシステムコールの情報を追加する。

| 番号 | 名前 | カテゴリ | 引数 | 戻り値 |
|---|---|---|---|---|
| 200 | `MY_NEW_CALL` | カテゴリ | arg1: 説明, arg2: 説明 | `os_status_t` |

## Checklist

- [ ] `Syscall_Main.h` に番号を定義
- [ ] ハンドラ関数を実装 (引数バリデーション、ケーパビリティチェック含む)
- [ ] `Syscall_Dispatch.c` にディスパッチ登録
- [ ] 新規ファイルの場合、`Makefile` の `KERNEL_C_SRCS` に追加
- [ ] `Userland/Syscalls.c` にラッパー追加
- [ ] `Userland/Syscalls.h` にプロトタイプ追加
- [ ] `Syscall_Reference.md` を更新
- [ ] `make clean && make` でビルド成功を確認
