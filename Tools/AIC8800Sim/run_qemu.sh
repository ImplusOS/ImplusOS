#!/usr/bin/env bash
# Boot ImplusOS in QEMU with a virtual AIC8800D80 attached and report whether
# the Wi-Fi stack got all the way to IP traffic.
#
#   Tools/AIC8800Sim/run_qemu.sh [--warm] [--timeout SEC] [--keep] [--gui]
#
# The LiveCD image must have been built with the probe autostarted:
#   make image_livecd AUTOSTART=/Userland/WifiProbe/WifiProbe.ELF
#
# --warm     run twice against the same simulator: the second boot finds the
#            adapter still in its firmware stage (what a reboot of a real PC
#            does), so the driver has to reboot it back to the ROM loader.
# --gui      show the QEMU window (default: headless)
# --keep     leave the logs in place and print where they are
#
# Exit status 0 only when the guest printed "[wifiprobe] RESULT: PASS" and
# the simulator recorded no failed check, for every boot.
set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
ARCH=x86_64
ISO="$ROOT/Image/ImplusOS-$ARCH-LiveCD.iso"
FW_DIR="$ROOT/Kernel/Source/Drivers/Firmware/AIC8800"
OVMF_CODE="$ROOT/OVMF_CODE_4M.fd"
OVMF_VARS_TEMPLATE="$ROOT/OVMF_VARS_4M.fd"
PORT=${AIC_SIM_PORT:-7788}
TIMEOUT=300
WARM=0
GUI=0
KEEP=0
while [ $# -gt 0 ]; do
    case "$1" in
        --warm) WARM=1 ;;
        --gui) GUI=1 ;;
        --keep) KEEP=1 ;;
        --timeout) shift; TIMEOUT=$1 ;;
        *) echo "unknown option $1" >&2; exit 2 ;;
    esac
    shift
done

[ -f "$ISO" ] || { echo "missing $ISO -- make image_livecd AUTOSTART=/Userland/WifiProbe/WifiProbe.ELF" >&2; exit 2; }
command -v qemu-system-x86_64 >/dev/null || { echo "qemu-system-x86_64 not found" >&2; exit 2; }

OUT=$(mktemp -d "${TMPDIR:-/tmp}/aic8800sim.XXXXXX")
cp "$OVMF_VARS_TEMPLATE" "$OUT/OVMF_VARS.fd"

python3 -I "$ROOT/Tools/AIC8800Sim/aic8800_sim.py" --port "$PORT" --fw-dir "$FW_DIR" \
    --report "$OUT/sim-report.json" ${AIC_SIM_ARGS:-} >"$OUT/sim.log" 2>&1 &
SIM_PID=$!
cleanup() {
    [ -n "${QEMU_PID:-}" ] && kill "$QEMU_PID" 2>/dev/null
    kill "$SIM_PID" 2>/dev/null
    wait 2>/dev/null
    if [ "$KEEP" = 1 ]; then echo "logs: $OUT"; else rm -rf "$OUT"; fi
}
trap cleanup EXIT

for _ in $(seq 50); do
    grep -q listening "$OUT/sim.log" 2>/dev/null && break
    sleep 0.1
done
grep -q listening "$OUT/sim.log" || { cat "$OUT/sim.log"; echo "simulator did not start" >&2; exit 1; }

KVM=()
[ -w /dev/kvm ] && KVM=(-enable-kvm -cpu host)
DISPLAY_ARGS=(-display none)
[ "$GUI" = 1 ] && DISPLAY_ARGS=(-display gtk)

start_qemu() {   # $1 = label, $2 = serial log
    # Run from $OUT so the monitor socket path stays under the 108-byte limit.
    (cd "$OUT" && exec qemu-system-x86_64 \
        -machine q35 "${KVM[@]}" -m 4096 -smp 2 \
        -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
        -drive if=pflash,format=raw,file="$OUT/OVMF_VARS.fd" \
        -device ich9-ahci,id=sata \
        -drive if=none,id=cdrom0,file="$ISO",media=cdrom,format=raw,readonly=on \
        -device ide-cd,drive=cdrom0,bus=sata.0,bootindex=0 \
        -device qemu-xhci,id=xhci \
        -device usb-kbd,bus=xhci.0 -device usb-tablet,bus=xhci.0 \
        -chardev socket,id=aic,host=127.0.0.1,port="$PORT" \
        -device usb-redir,chardev=aic,id=aic8800,bus=xhci.0 \
        -nic none \
        "${DISPLAY_ARGS[@]}" -serial file:"$2" \
        -monitor unix:"monitor-$1.sock",server,nowait \
        ${QEMU_EXTRA:-} 2>>"$OUT/qemu-$1.log") &
    QEMU_PID=$!
}

# OVMF (Ubuntu's OVMF_CODE_4M.fd under KVM) intermittently stalls on the
# TianoCore logo with this LiveCD -- before it hands over to the boot loader,
# so before any ImplusOS code runs and before it has touched the USB bus. The
# same happens without the simulated adapter attached. A boot that has not
# reached "BdsDxe: starting" after FIRMWARE_WAIT seconds is restarted (and
# counted in the report); nothing the guest or the adapter did is retried.
FIRMWARE_WAIT=${FIRMWARE_WAIT:-40}
FIRMWARE_TRIES=${FIRMWARE_TRIES:-5}

boot() {   # $1 = label
    local serial="$OUT/serial-$1.log" try
    for try in $(seq "$FIRMWARE_TRIES"); do
        : >"$serial"
        start_qemu "$1" "$serial"
        local fw_deadline=$((SECONDS + FIRMWARE_WAIT))
        while [ $SECONDS -lt $fw_deadline ] && ! grep -aq "BdsDxe: starting" "$serial"; do
            kill -0 "$QEMU_PID" 2>/dev/null || break
            sleep 1
        done
        grep -aq "BdsDxe: starting" "$serial" && break
        echo "boot $1: OVMF stalled before the boot loader (attempt $try), restarting QEMU"
        kill "$QEMU_PID" 2>/dev/null; wait "$QEMU_PID" 2>/dev/null; QEMU_PID=
        sleep 1
    done
    local deadline=$((SECONDS + TIMEOUT)) result=TIMEOUT
    while [ $SECONDS -lt $deadline ]; do
        if grep -q "\[wifiprobe\] RESULT: PASS" "$serial"; then result=PASS; break; fi
        if grep -q "\[wifiprobe\] RESULT: FAIL" "$serial"; then result=FAIL; break; fi
        kill -0 "$QEMU_PID" 2>/dev/null || { result=QEMU-EXITED; break; }
        sleep 1
    done
    # Let the access point finish its side (ping, group rekey) before stopping.
    [ "$result" = PASS ] && sleep 8
    kill "$QEMU_PID" 2>/dev/null; wait "$QEMU_PID" 2>/dev/null; QEMU_PID=
    echo "---- guest ($1): Wi-Fi lines ----"
    grep -a -E "\[wifiprobe\]|\[AIC8800\]|\[USB\] dev vid=a69c|vendor iface" "$serial" | tail -60
    echo "---- boot $1: $result"
    [ "$result" = PASS ]
}

rc=0
boot cold || rc=1
if [ "$WARM" = 1 ]; then
    sleep 1
    boot warm || rc=1
fi
kill -INT "$SIM_PID" 2>/dev/null; wait "$SIM_PID" 2>/dev/null
echo "---- simulator ----"
sed -n '/==== AIC8800 simulator summary/,$p' "$OUT/sim.log" | tail -40
grep -q "^  FAIL" "$OUT/sim.log" && rc=1
grep -q " 0 failed" "$OUT/sim.log" || rc=1
[ "$rc" = 0 ] && echo "AIC8800 QEMU test: PASS" || echo "AIC8800 QEMU test: FAIL"
exit $rc
