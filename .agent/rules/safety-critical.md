# Safety-Critical Rules

カーネル開発における安全性に関する必須ルール。これらのルールは例外なく遵守すること。

## Interrupt Safety

- クリティカルセクションでは **必ず** IRQ を保護する。

```c
uint64_t flags = irq_save_disable();
spinlock_lock(&lock);
// critical section — 割り込み無効
spinlock_unlock(&lock);
irq_restore(flags);
```

- `irq_save_disable()` と `irq_restore()` は必ず対で使用する。
- クリティカルセクション内で長時間ブロックする操作を行わないこと。

## Spinlock Protocol

- スピンロックは TTAS (test-and-test-and-set) パターンで実装されている (`Kernel/Sync/Spinlock.h`)。
- `spinlock_lock()` / `spinlock_unlock()` を必ず対で使用する。
- **デッドロック防止**: 複数のロックを取得する場合は、常に同じ順序で取得する。
- スピンロック保持中に:
  - メモリ割り当て (`malloc`) を呼ばない (再帰ロックの危険)
  - スリープ / ブロック操作を行わない

## Memory Safety

### NULL チェック

- ポインタ引数は使用前に **必ず** NULL チェックを行う。
- NULL ポインタアクセスは即座にカーネルパニックの原因となる。

```c
if (buffer == NULL) {
    return OS_STATUS_INVALID_ARG;
}
```

### バッファサイズの検証

- ユーザーからのバッファサイズやオフセットは、使用前に上限チェックを行う。
- 整数オーバーフローが発生しないよう、サイズ計算時は慎重にキャストする。

### DMA メモリ

- DMA バッファは `dma_alloc()` / `dma_free()` で管理する。
- DMA メモリは物理連続でアイデンティティマッピングされている。
- 通常の `malloc()` で確保したメモリを DMA に使用してはならない。

### 機密データ

- 機密性のあるデータを扱う場合は `malloc_sensitive()` / `free_sensitive()` を使用する。
- `free_sensitive()` は解放前にメモリをゼロクリアする。

## NX (No-Execute) Bit

- データ領域 (スタック、ヒープ、DMA バッファ) には **実行権限を付与しない**。
- NX ビットは `init_paging()` でグローバルに有効化されている。
- 新たなページマッピングを追加する際は、適切なフラグを設定すること。

## Panic

- 回復不能なエラーは `panic(const char *msg)` を使用して停止する。
- panic はカーネルのすべての実行を停止するため、可能な限りエラーコードで処理すること。
- パニック前にシリアル出力でデバッグ情報を出力する。

```c
if (critical_failure) {
    serial_write_string("FATAL: subsystem XYZ failed\n");
    panic("subsystem XYZ: unrecoverable error");
}
```

## Process Capabilities

- 新しい機能をシステムコール経由で公開する場合は、適切なケーパビリティフラグでゲートすること。
- 既存のケーパビリティフラグ (`PROCESS_CAP_*`) の意味を変更してはならない。
- 定義: `Kernel/ProcessManager/ProcessManager.h`

| フラグ | 値 | 用途 |
|---|---|---|
| `PROCESS_CAP_SERIAL` | `1 << 0` | シリアル出力 |
| `PROCESS_CAP_PROCESS` | `1 << 1` | プロセス管理 |
| `PROCESS_CAP_FILE` | `1 << 2` | ファイル I/O |
| `PROCESS_CAP_MEMORY` | `1 << 3` | メモリ割り当て |
| `PROCESS_CAP_INPUT` | `1 << 4` | 入力デバイス |
| `PROCESS_CAP_SIGNAL` | `1 << 5` | シグナル処理 |
| `PROCESS_CAP_IPC` | `1 << 6` | プロセス間通信 |
| `PROCESS_CAP_NETWORK` | `1 << 7` | ネットワークアクセス |

## Syscall Input Validation

- ユーザーランドから渡されるすべての引数を **信頼しない**。
- ポインタがユーザー空間の範囲内であることを確認する。
- サイズ・オフセット・インデックスの範囲チェックを行う。
- 不正な引数には `OS_STATUS_INVALID_ARG` を返す。

## SMP (Multi-Core) Safety

- 共有データへのアクセスは必ずスピンロックまたはアトミック操作で保護する。
- TLB shootdown (`IPI vector 0xFE`) は、ページテーブルを変更した後に必ず実行する。
- Per-CPU データへのアクセス時は、割り込みを無効にして CPU の切り替わりを防ぐ。
