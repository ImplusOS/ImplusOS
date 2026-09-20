#!/usr/bin/env bash
# stage-alsa.sh — alsa-lib（libasound.so.2、閉包に既にある）の設定ファイルを
#                 STAGE_DIR へ配置する。stage.sh（.so 閉包）の後に走らせる。
#
# libasound は起動時に /usr/share/alsa/alsa.conf を読み、"default" や
# "hw:0,0" といった PCM 名をそこで定義された型（hw / plug …）へ解決する。
# Debian ではこのファイルは libasound2-data にあるが、中身の大半は存在しない
# ハードウェア向けのカード別設定と PulseAudio 連携なので、ここでは
# ImplusOS のカーネルが提供する 1 枚のカード（/dev/snd/controlC0,
# /dev/snd/pcmC0D0p — Kernel/Source/Core/sound/ALSA.c）に必要な定義だけを
# 書き出す。
#
# カーネル側のデバイスは U8/S16_LE/S32_LE/FLOAT_LE、1〜8ch、8k〜192kHz を
# 受け付けて自前で 48kHz ステレオへ変換するので、"default" は plug を挟まず
# 直接 hw を指す（plug は明示的に "plug:..." と書いた場合だけ使われる）。
#
# 入力(env): STAGE_DIR
set -euo pipefail
: "${STAGE_DIR:?}"

log(){ printf '[alsa] %s\n' "$*" >&2; }

D="$STAGE_DIR/usr/share/alsa"
mkdir -p "$D"
cat > "$D/alsa.conf" <<'CONF'
# ImplusOS: minimal alsa-lib configuration (Vendor/LinuxRuntime/stage-alsa.sh).
# One card, served by the kernel (Kernel/Source/Core/sound/ALSA.c).

defaults.ctl.card 0
defaults.pcm.card 0
defaults.pcm.device 0
defaults.pcm.subdevice -1
defaults.namehint.showall off
defaults.namehint.basic on
defaults.namehint.extended off

pcm.hw {
	@args [ CARD DEV SUBDEV ]
	@args.CARD { type string default "0" }
	@args.DEV { type integer default 0 }
	@args.SUBDEV { type integer default -1 }
	type hw
	card $CARD
	device $DEV
	subdevice $SUBDEV
	hint { show on description "Direct hardware device" }
}

pcm.plughw {
	@args [ CARD DEV SUBDEV ]
	@args.CARD { type string default "0" }
	@args.DEV { type integer default 0 }
	@args.SUBDEV { type integer default -1 }
	type plug
	slave.pcm {
		type hw
		card $CARD
		device $DEV
		subdevice $SUBDEV
	}
}

pcm.plug {
	@args [ SLAVE ]
	@args.SLAVE { type string }
	type plug
	slave.pcm $SLAVE
}

pcm.default {
	type hw
	card 0
	device 0
	hint { show on description "ImplusOS Audio" }
}

pcm.sysdefault {
	type hw
	card 0
	device 0
}

ctl.hw {
	@args [ CARD ]
	@args.CARD { type string default "0" }
	type hw
	card $CARD
}

ctl.default {
	type hw
	card 0
}

ctl.sysdefault {
	type hw
	card 0
}
CONF
log "wrote $D/alsa.conf"
