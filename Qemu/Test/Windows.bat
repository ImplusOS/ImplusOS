@echo off

"C:\Program Files\qemu\qemu-system-x86_64.exe" ^
  -machine pc ^
  -smp 4 ^
  -m 2G ^
  -device qemu-xhci,id=xhci ^
  -netdev user,id=net0 ^
  -device virtio-net-pci,netdev=net0 ^
  -drive if=pflash,format=raw,readonly=on,file=Resource\OVMF_CODE.fd ^
  -drive if=none,id=usbstick,format=raw,file=Resource\ImplusOS.iso ^
  -device usb-storage,drive=usbstick ^
  -serial stdio ^
  -device usb-kbd,bus=xhci.0 ^
  -device usb-mouse,bus=xhci.0

pause