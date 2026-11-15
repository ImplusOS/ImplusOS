# AIC8800Sim — virtual AIC8800D80 for QEMU

`aic8800_sim.py` is a software AIC8800D80 USB Wi-Fi adapter. QEMU's
`usb-redir` device forwards one guest USB device over a socket using the
usbredir protocol, and this script answers on that socket as the device. The
guest kernel's xHCI driver and the AIC8800 driver
(`Kernel/Source/Drivers/Wi-Fi/AIC8800`) run unmodified, exactly as they would
against the real dongle.

## What it models

| Layer | Behaviour |
|---|---|
| USB | High-speed device and standard descriptors. Answers SET_CONFIGURATION and cancel. Bulk IN requests stay pending until data exists (NAK). Re-enumeration (disconnect → ack → connect) |
| ROM loader `a69c:8d80` | One bulk pair. `DBG_MEM_READ/WRITE/BLOCK_WRITE` into a word-granular chip memory, with the chip revision word at `0x40500000`. `DBG_START_APP` checks that the firmware images sit at the vendor addresses, then the device re-enumerates as `8d81` |
| Firmware `a69c:8d81` | Two bulk pairs: data, plus a message pipe. Confirms every bring-up command and checks it follows the vendor order. Scan returns 4 BSSs, with several indications per transfer. Sends `SM_CONNECT` / `SM_CONNECT_IND`. Checks `MM_KEY_ADD` against the AP's own keys, and checks `ME_SET_CONTROL_PORT`. Aggregates RX data into ≤20 KiB transfers. `DBG_START_APP(REBOOT)` sends it back to the ROM loader (warm boot) |
| Access point "ImplusSim" | WPA2-PSK/CCMP. Runs the 4-way handshake on its own (PBKDF2, PRF-SHA1, HMAC-SHA1 MIC, AES key wrap via `cryptography`), retransmits M1, and rekeys the group key once traffic flows. RX frames carry the CCMP header with decr_status=CCMP, as the hardware delivers them |
| LAN 192.168.88.0/24 | ARP, a DHCP server (`.100`, DNS = gateway `.1`), ICMP echo in both directions (the AP pings the guest after the lease), UDP echo and minimal TCP echo on port 7 |

Every protocol expectation is recorded as a check. The script prints a summary
when it exits and, with `--report`, also writes it as JSON.

## Running the end-to-end test

```bash
make image_livecd AUTOSTART=/Userland/WifiProbe/WifiProbe.ELF
Tools/AIC8800Sim/run_qemu.sh            # cold boot
Tools/AIC8800Sim/run_qemu.sh --warm     # cold boot, then a reboot with the adapter still running firmware
```

`Userland/Application/WifiProbe` drives the Wi-Fi API headlessly, the way the
panel would:

1. wait for READY
2. scan
3. connect
4. wait for a DHCP lease
5. UDP echo, then TCP echo

It prints `[wifiprobe] RESULT: PASS|FAIL <step>` on COM1. `run_qemu.sh` passes
only when the probe passed and the simulator recorded no failed check.
Options:

- `--gui`: show the QEMU window
- `--keep`: keep the serial and simulator logs
- `--timeout SEC`

To run the simulator by hand:

```bash
python3 Tools/AIC8800Sim/aic8800_sim.py --port 7788 \
    --fw-dir Kernel/Source/Drivers/Firmware/AIC8800 [--trace-data] [-v]
qemu-system-x86_64 ... -device qemu-xhci,id=xhci \
    -chardev socket,id=aic,host=127.0.0.1,port=7788 \
    -device usb-redir,chardev=aic,bus=xhci.0
```

QEMU only accepts a redirected device on xHCI when the peer offers the
usbredir capabilities `64bits_ids`, `32bits_bulk_length` and
`ep_info_max_packet_size`. The simulator offers all three.

`-v` logs every control and bulk transaction. That slows the firmware
download a lot (thousands of 1 KiB block writes), so use `--trace-data` to
watch only the firmware-stage pipes.

## Limits

The simulator models the firmware's host interface, not its radio. Several
things are taken on trust from the vendor driver rather than tested here:

- the HT capabilities the driver sends in `ME_CONFIG`
- the TX-power and calibration tables
- the content of the patch table
- 5 GHz behaviour

A test run against this simulator does not replace a run on the real dongle.
