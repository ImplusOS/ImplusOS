#!/bin/sh
# usage: boot.sh <dir>
D=$1; mkdir -p $D; cp OVMF_VARS_4M.fd $D/vars.fd
SDL_VIDEODRIVER=x11 exec qemu-system-x86_64 -machine q35 -cpu host -enable-kvm -m 8192 -smp 4 \
 -device ich9-ahci,id=sata -device qemu-xhci,id=xhci -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0 \
 -netdev user,id=net0 -device virtio-net-pci,netdev=net0 -vga none -device virtio-gpu-gl-pci,id=gpu0 -device VGA -display sdl,gl=es \
 -serial file:$D/serial.log -monitor unix:$D/mon.sock,server,nowait -pidfile $D/pid \
 -drive if=pflash,format=raw,readonly=on,file=OVMF_CODE_4M.fd -drive if=pflash,format=raw,file=$D/vars.fd \
 -drive if=none,id=cdrom0,file=Image/ImplusOS-x86_64-LiveCD.iso,media=cdrom,format=raw,snapshot=on \
 -device ide-cd,drive=cdrom0,bus=sata.0,bootindex=0 > $D/qemu.out 2>&1
