# Workflow: Adding a Userland Application

ImplusOS に新しいユーザーランドアプリケーションを追加する手順。

## Overview

```
Userland/Application/UserApps/com_ImplusOS_<name>/  ← 1. ディレクトリ作成
  ├── main.c                                         ← 2. アプリ実装
  └── Makefile                                       ← 3. ビルド定義
Makefile (top-level)                                  ← 4. app_build ターゲットに追加
```

## Step 1: ディレクトリ作成

```bash
mkdir -p Userland/Application/UserApps/com_ImplusOS_<name>
```

**命名規則**: `com_ImplusOS_<name>` — 逆ドメイン形式のアプリ識別子。

### System Apps vs User Apps

| ディレクトリ | 用途 |
|---|---|
| `Userland/Application/SystemApps/` | OS のコアサービス (WindowManager, Shell 等) |
| `Userland/Application/UserApps/` | ユーザーアプリケーション |

## Step 2: アプリケーションを実装

```c
#include "Userland/Syscalls.h"
// 必要に応じて Userland/API/ のヘッダを include
// #include "Userland/API/Window.h"
// #include "Userland/API/Graphics.h"
// #include "Userland/API/File.h"

int main(void) {
    // アプリケーションロジック

    // シリアル出力 (デバッグ)
    serial_putchar('H');

    // ファイル操作
    // int fd = file_open("/path/to/file", 0);

    // ウィンドウ作成
    // window_create(x, y, w, h);

    // メインループ
    while (1) {
        process_yield();
    }

    return 0;
}
```

### 利用可能な API

| ヘッダ | 機能 |
|---|---|
| `Userland/Syscalls.h` | 低レベルシステムコールラッパー |
| `Userland/API/Process.h` | プロセス管理 |
| `Userland/API/File.h` | ファイル I/O |
| `Userland/API/Memory.h` | メモリ割り当て |
| `Userland/API/Input.h` | キーボード / マウス入力 |
| `Userland/API/IPC.h` | プロセス間通信 |
| `Userland/API/Graphics.h` | グラフィックス描画 |
| `Userland/API/Window.h` | ウィンドウ管理 |
| `Userland/API/Network.h` | ネットワーク |
| `Userland/API/Socket.h` | Berkeley ソケット API |
| `Userland/API/Serial.h` | シリアルデバッグ出力 |

## Step 3: Makefile を作成

既存アプリの Makefile を参考にする (例: `com_ImplusOS_exampleApp`)。

```makefile
APP_NAME := com_ImplusOS_<name>

include ../../AppCommon.mk

SRCS := main.c
OBJS := $(SRCS:.c=.o)

all: $(APP_NAME).ELF

%.o: %.c
	$(CC) $(USERLAND_CFLAGS) -c $< -o $@

$(APP_NAME).ELF: $(OBJS) $(USERLAND_APP_OBJS)
	$(LD) $(USERLAND_APP_LDFLAGS) $^ -o $@
	@mkdir -p $(BUILD_DIR)/Userland/UserApps/$(APP_NAME)
	@cp $(APP_NAME).ELF $(BUILD_DIR)/Userland/UserApps/$(APP_NAME)/

clean:
	rm -f $(OBJS) $(APP_NAME).ELF
```

**重要**: `AppCommon.mk` (`Userland/AppCommon.mk`) を include すること。共通のコンパイルフラグと依存オブジェクトが定義されている。

## Step 4: トップレベル Makefile に追加

`Makefile` の `app_build` ターゲットにアプリのビルドを追加する。

```makefile
app_build: $(USERLAND_INIT_OBJS)
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_windowmanager
	# ... 既存のアプリ ...
	@$(MAKE) -C Userland/Application/UserApps/com_ImplusOS_<name>  # ← 追加
```

## Step 5: ビルドと動作確認

```bash
make clean && make
make image_esp
make run_usb
```

- ビルド後、`Build/Userland/UserApps/com_ImplusOS_<name>/` に ELF が生成される
- ISO イメージ内の `/Userland/UserApps/` に配置される

## アプリの起動

ユーザーランドの Init プロセス (`Userland/Userland.c`) または Shell (`com_ImplusOS_shell`) からアプリの ELF パスを指定して起動する:

```c
process_spawn_elf("/Userland/UserApps/com_ImplusOS_<name>/com_ImplusOS_<name>.ELF");
```

## Checklist

- [ ] `Userland/Application/UserApps/com_ImplusOS_<name>/` ディレクトリを作成
- [ ] `main.c` を実装
- [ ] `Makefile` を作成 (`AppCommon.mk` を include)
- [ ] トップレベル `Makefile` の `app_build` にエントリ追加
- [ ] `make clean && make` でビルド成功を確認
- [ ] `make image_esp && make run_usb` で動作確認
