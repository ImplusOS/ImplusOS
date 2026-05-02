#!/bin/bash

qemu-system-x86_64 \
    -machine q35,accel=kvm \
    -enable-kvm \
    -smp 4 \
    -m 4G \
    -cpu host \
    -device intel-iommu \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0,mac=52:54:00:12:34:56 \
    -drive if=pflash,format=raw,readonly=on,file=Resource/OVMF_CODE.fd \
    -drive file=Resource/ImplusOS.iso,media=cdrom \
    -device ich9-ahci,id=sata \
    -device ich9-intel-hda \
    -device hda-duplex \
    -rtc base=localtime,clock=host \
    -global ICH9-LPC.disable_s3=0 \
    -global ICH9-LPC.disable_s4=0 \
    -smbios type=1,manufacturer="Dell Inc.",product="XPS 8940" \
    -serial stdio

read -p "Press Enter to continue..."