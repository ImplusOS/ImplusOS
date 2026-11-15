#!/usr/bin/env python3
"""Virtual AIC8800D80 USB Wi-Fi adapter for QEMU.

QEMU's ``usb-redir`` device forwards every USB transaction of one guest
device over a socket (the usbredir protocol, normally spoken by
``usbredirserver`` on top of a real device).  This script is the other end
of that socket: it *is* the device.  The guest's xHCI driver enumerates it,
the guest's AIC8800 driver loads firmware into it and drives it, and it
behaves the way the real dongle does at the USB level:

  * ROM loader stage   a69c:8d80, one bulk pair, TASK_DBG memory commands
                       (MEM_READ / MEM_WRITE / MEM_BLOCK_WRITE / START_APP).
                       Every block write lands in a modelled chip memory;
                       START_APP checks that the firmware images are where
                       the vendor driver puts them, then the device drops
                       off the bus and re-enumerates --
  * firmware stage     a69c:8d81, two bulk pairs (data, message pipe), the
                       full-MAC command set the driver uses (MM_*, ME_*,
                       SCANU_*, SM_*), RX aggregation of data packets into
                       one transfer, and DBG_START_APP(REBOOT) back to the
                       ROM loader (the warm-boot path).

Behind the "radio" sits a small world:

  * four networks in every scan (WPA2-CCMP, open, WPA2 with a TKIP group
    cipher, WPA3-only);
  * a WPA2-PSK access point that runs the real 4-way handshake (PRF-SHA1,
    HMAC-SHA1 MICs, AES key wrap of the GTK -- implemented here
    independently of the guest driver) and, once the link is up, a group
    key rekey;
  * an IPv4 network: ARP, a DHCP server, ICMP echo both ways (the AP pings
    the guest after the lease), UDP echo and a minimal TCP echo on port 7.

Every protocol expectation is recorded as a check; the summary is printed
on exit and written to --report as JSON.  run_qemu.sh drives the whole
thing.

Usage:  aic8800_sim.py --port 7788 --fw-dir Kernel/Source/Drivers/Firmware/AIC8800
"""

import argparse
import asyncio
import hashlib
import hmac
import json
import os
import random
import signal
import struct
import sys
import time

try:
    from cryptography.hazmat.primitives.keywrap import aes_key_wrap
except ImportError:  # pragma: no cover
    sys.exit("aic8800_sim.py needs the 'cryptography' package (pip install cryptography)")

# ----------------------------------------------------------------------------
# logging and checks
# ----------------------------------------------------------------------------

T0 = time.monotonic()
VERBOSE = False
TRACE_DATA = False
CONNECT_DELAY = 2.0
CHECKS = []          # (ok, text)
EVENTS = []          # milestone names, in order


def log(*a):
    print("[sim %7.3f]" % (time.monotonic() - T0), *a, flush=True)


def vlog(*a):
    if VERBOSE:
        log(*a)


def check(cond, text):
    CHECKS.append((bool(cond), text))
    log(("ok    " if cond else "FAIL  ") + text)
    return cond


def milestone(name):
    if name not in EVENTS:
        EVENTS.append(name)
        log("== " + name)


# ----------------------------------------------------------------------------
# usbredir protocol (usbredirproto.h), the "usb-host" side
# ----------------------------------------------------------------------------

R_HELLO, R_DEVICE_CONNECT, R_DEVICE_DISCONNECT, R_RESET = 0, 1, 2, 3
R_INTERFACE_INFO, R_EP_INFO = 4, 5
R_SET_CONFIGURATION, R_GET_CONFIGURATION, R_CONFIGURATION_STATUS = 6, 7, 8
R_SET_ALT_SETTING, R_GET_ALT_SETTING, R_ALT_SETTING_STATUS = 9, 10, 11
R_START_ISO, R_STOP_ISO = 12, 13
R_START_INTR, R_STOP_INTR = 15, 16
R_ALLOC_STREAMS, R_FREE_STREAMS = 18, 19
R_CANCEL_DATA_PACKET = 21
R_FILTER_REJECT, R_FILTER_FILTER = 22, 23
R_DEVICE_DISCONNECT_ACK = 24
R_START_BULK_RECV, R_STOP_BULK_RECV = 25, 26
R_CONTROL_PACKET, R_BULK_PACKET, R_ISO_PACKET, R_INTERRUPT_PACKET = 100, 101, 102, 103

CAP_CONNECT_DEVICE_VERSION = 1 << 1
CAP_DEVICE_DISCONNECT_ACK = 1 << 3
CAP_EP_INFO_MAX_PACKET_SIZE = 1 << 4
CAP_64BITS_IDS = 1 << 5
CAP_32BITS_BULK_LENGTH = 1 << 6
# QEMU refuses a redirected device on an xHCI bus unless the peer has
# ep_info_max_packet_size, 32bits_bulk_length and 64bits_ids.
OUR_CAPS = (CAP_CONNECT_DEVICE_VERSION | CAP_DEVICE_DISCONNECT_ACK | CAP_EP_INFO_MAX_PACKET_SIZE |
            CAP_64BITS_IDS | CAP_32BITS_BULK_LENGTH)

ST_SUCCESS, ST_CANCELLED, ST_INVAL, ST_IOERROR, ST_STALL = 0, 1, 2, 3, 4
SPEED_HIGH = 2
EP_CONTROL, EP_BULK, EP_INVALID = 0, 2, 255


def ep_index(addr):
    return (addr & 0x0F) | (0x10 if addr & 0x80 else 0)


# ----------------------------------------------------------------------------
# the device's USB personality per stage
# ----------------------------------------------------------------------------

VID = 0xA69C
PID_LOADER = 0x8D80
PID_FDRV = 0x8D81

EP_DATA_IN, EP_DATA_OUT, EP_MSG_IN, EP_MSG_OUT = 0x81, 0x01, 0x82, 0x02


def usb_string(s):
    b = s.encode("utf-16-le")
    return bytes([2 + len(b), 3]) + b


class Personality:
    def __init__(self, pid, endpoints, product):
        self.pid = pid
        self.endpoints = endpoints            # [(addr, mps)]
        self.device = struct.pack("<BBHBBBBHHHBBBB", 18, 1, 0x0200, 0, 0, 0, 64,
                                  VID, pid, 0x0100, 1, 2, 3, 1)
        iface = struct.pack("<BBBBBBBBB", 9, 4, 0, 0, len(endpoints), 0xFF, 0xFF, 0xFF, 0)
        eps = b"".join(struct.pack("<BBBBHB", 7, 5, a, 2, m, 0) for a, m in endpoints)
        total = 9 + len(iface) + len(eps)
        self.config = struct.pack("<BBHBBBBB", 9, 2, total, 1, 1, 0, 0x80, 250) + iface + eps
        self.strings = {0: bytes([4, 3, 0x09, 0x04]), 1: usb_string("AICSemi"),
                        2: usb_string(product), 3: usb_string("20250615")}


LOADER = Personality(PID_LOADER, [(EP_DATA_IN, 512), (EP_DATA_OUT, 512)], "AIC8800D80 (ROM)")
FDRV = Personality(PID_FDRV, [(EP_DATA_IN, 512), (EP_DATA_OUT, 512),
                              (EP_MSG_IN, 512), (EP_MSG_OUT, 512)], "AIC8800D80")

# ----------------------------------------------------------------------------
# AIC8800 messages
# ----------------------------------------------------------------------------

TASK_MM, TASK_DBG, TASK_SCANU, TASK_ME, TASK_SM = 0, 1, 4, 5, 6


def msg(task, n):
    return (task << 10) | n


DBG_MEM_READ, DBG_MEM_WRITE, DBG_BLOCK_WRITE, DBG_START_APP = (
    msg(TASK_DBG, 0), msg(TASK_DBG, 2), msg(TASK_DBG, 11), msg(TASK_DBG, 13))
MM_RESET, MM_START, MM_VERSION, MM_ADD_IF = msg(TASK_MM, 0), msg(TASK_MM, 2), msg(TASK_MM, 4), msg(TASK_MM, 6)
MM_KEY_ADD, MM_KEY_DEL, MM_SET_PS = msg(TASK_MM, 0x24), msg(TASK_MM, 0x26), msg(TASK_MM, 0x31)
MM_SET_COEX, MM_RF_CALIB, MM_GET_MAC = msg(TASK_MM, 0x65), msg(TASK_MM, 0x69), msg(TASK_MM, 0x73)
MM_TXPWR, MM_STACK_START = msg(TASK_MM, 0x77), msg(TASK_MM, 0x7B)
ME_CONFIG, ME_CHAN_CONFIG, ME_CONTROL_PORT = msg(TASK_ME, 0), msg(TASK_ME, 2), msg(TASK_ME, 4)
SCANU_START, SCANU_START_CFM, SCANU_RESULT_IND, SCANU_CFM_ADDITIONAL = (
    msg(TASK_SCANU, 0), msg(TASK_SCANU, 1), msg(TASK_SCANU, 4), msg(TASK_SCANU, 9))
SM_CONNECT, SM_CONNECT_IND, SM_DISCONNECT, SM_DISCONNECT_IND = (
    msg(TASK_SM, 0), msg(TASK_SM, 2), msg(TASK_SM, 3), msg(TASK_SM, 5))

MSG_NAMES = {
    MM_RESET: "MM_RESET", MM_START: "MM_START", MM_VERSION: "MM_VERSION", MM_ADD_IF: "MM_ADD_IF",
    MM_KEY_ADD: "MM_KEY_ADD", MM_KEY_DEL: "MM_KEY_DEL", MM_SET_PS: "MM_SET_PS_MODE",
    MM_SET_COEX: "MM_SET_COEX", MM_RF_CALIB: "MM_SET_RF_CALIB", MM_GET_MAC: "MM_GET_MAC_ADDR",
    MM_TXPWR: "MM_SET_TXPWR_IDX_LVL", MM_STACK_START: "MM_SET_STACK_START",
    ME_CONFIG: "ME_CONFIG", ME_CHAN_CONFIG: "ME_CHAN_CONFIG", ME_CONTROL_PORT: "ME_SET_CONTROL_PORT",
    SCANU_START: "SCANU_START", SM_CONNECT: "SM_CONNECT", SM_DISCONNECT: "SM_DISCONNECT",
    DBG_MEM_READ: "DBG_MEM_READ", DBG_MEM_WRITE: "DBG_MEM_WRITE",
    DBG_BLOCK_WRITE: "DBG_MEM_BLOCK_WRITE", DBG_START_APP: "DBG_START_APP",
}

# The order the vendor fdrv brings the station up in (rwnx_platform_on +
# rwnx_cfg80211_init + add_iface for an AIC8800D80).
VENDOR_BRING_ORDER = [MM_STACK_START, MM_TXPWR, MM_RF_CALIB, MM_GET_MAC, MM_RESET, MM_VERSION,
                      ME_CONFIG, ME_CHAN_CONFIG, MM_START, MM_SET_COEX, MM_ADD_IF]

FMAC_ADDR = 0x120000
ADID_ADDR = 0x201940
PATCH_ADDR = 0x20B43C
CHIP_REV_ADDR = 0x40500000
CHIP_REV_WORD = (0x03 << 16) | (1 << 25)     # U02, not an MCU part


def cfg_packet(msg_id, param=b""):
    """A CFG_CMD_RSP packet as the firmware sends it on the message pipe."""
    body = struct.pack("<HHHHI", msg_id, 0, 0, len(param), 0) + param
    pkt = struct.pack("<HBB", len(body), 0x11, 0) + body
    return pkt + b"\0" * ((-len(pkt)) % 4)


def data_packet(mpdu, decr=0, flags=0x40, vif=0, sta=0):
    """One RX data packet: hw_rxhdr (60 bytes, its first word the USB
    header) + the decrypted MPDU."""
    hdr = bytearray(60)
    struct.pack_into("<HBB", hdr, 0, len(mpdu), 0x00, 0)
    hdr[36] = (decr & 7) << 2
    hdr[48] = flags
    hdr[49] = vif
    hdr[50] = sta
    return bytes(hdr) + mpdu


# ----------------------------------------------------------------------------
# networks and the WPA2 access point
# ----------------------------------------------------------------------------

def prf_sha1(key, label, data, n):
    out, i = b"", 0
    while len(out) < n:
        out += hmac.new(key, label + b"\0" + data + bytes([i]), hashlib.sha1).digest()
        i += 1
    return out[:n]


RSN_CCMP = bytes.fromhex("30140100000fac040100000fac040100000fac020c00")
RSN_TKIP_GROUP = bytes.fromhex("30180100000fac020200000fac04000fac020100000fac020000")
RSN_SAE_ONLY = bytes.fromhex("30140100000fac040100000fac040100000fac08c000")

ETH_IP, ETH_ARP, ETH_EAPOL = 0x0800, 0x0806, 0x888E


def ip4(s):
    return bytes(int(x) for x in s.split("."))


def csum(b):
    if len(b) % 2:
        b += b"\0"
    s = sum(struct.unpack("!%dH" % (len(b) // 2), b))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF


def ip_packet(src, dst, proto, payload, ident=[0]):
    ident[0] = (ident[0] + 1) & 0xFFFF
    h = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + len(payload), ident[0], 0, 64, proto, 0, src, dst)
    return h[:10] + struct.pack("!H", csum(h)) + h[12:] + payload


def udp_segment(src, dst, sport, dport, payload):
    u = struct.pack("!HHHH", sport, dport, 8 + len(payload), 0) + payload
    ph = src + dst + struct.pack("!BBH", 0, 17, len(u))
    c = csum(ph + u) or 0xFFFF
    return u[:6] + struct.pack("!H", c) + u[8:]


def tcp_segment(src, dst, sport, dport, seq, ack, flags, payload=b"", window=8192):
    h = struct.pack("!HHIIBBHHH", sport, dport, seq, ack, 5 << 4, flags, window, 0, 0)
    ph = src + dst + struct.pack("!BBH", 0, 6, len(h) + len(payload))
    c = csum(ph + h + payload)
    return h[:16] + struct.pack("!H", c) + h[18:] + payload


class AccessPoint:
    """The WPA2-PSK network the guest is expected to join, plus its LAN."""

    def __init__(self, chip, ssid, psk):
        self.chip = chip
        self.ssid, self.psk = ssid.encode(), psk.encode()
        self.bssid = bytes.fromhex("02a1c8800d80")
        self.freq = 2437
        self.pmk = hashlib.pbkdf2_hmac("sha1", self.psk, self.ssid, 4096, 32)
        self.ip = ip4("192.168.88.1")
        self.lease_ip = ip4("192.168.88.100")
        self.reset()

    def reset(self):
        self.sta = None
        self.sta_idx = 0
        self.anonce = os.urandom(32)
        self.gtk = os.urandom(16)
        self.gtk_idx = 1
        self.replay = 0
        self.ptk = None
        self.keys_installed = False      # TK installed by the guest (MM_KEY_ADD)
        self.secure = False              # 4-way finished (message 4 received)
        self.rekeyed = False
        self.rekey_pending = None
        self.m1_sent_at = None
        self.lease_given = False
        self.ping_id = None
        self.tcp = {}

    # --- 802.11 framing toward the guest
    def mpdu(self, sa, ethertype, payload, protected):
        hdr = b"\x88\x02" + b"\0\0" + self.sta + self.bssid + sa + b"\x10\x00" + b"\x00\x00"
        sec = b""
        if protected:
            pn = self.chip.next_pn()
            sec = bytes([pn & 0xFF, (pn >> 8) & 0xFF, 0, 0x20 | 0, (pn >> 16) & 0xFF,
                         (pn >> 24) & 0xFF, 0, 0])
        return hdr + sec + b"\xaa\xaa\x03\x00\x00\x00" + struct.pack(">H", ethertype) + payload

    def to_guest(self, ethertype, payload, sa=None):
        # Once the pairwise key is in, frames arrive CCMP-protected: the
        # hardware decrypts them but leaves the 8-byte CCMP header in place
        # and reports decr_status CCMP_SUCCESS (3) in the RX descriptor.
        prot = self.keys_installed
        mp = self.mpdu(sa or self.bssid, ethertype, payload, prot)
        self.chip.queue_data(data_packet(mp, decr=3 if prot else 0, sta=self.sta_idx))

    # --- EAPOL
    def eapol_key(self, key_info, key_len, nonce, key_data, mic_key=None):
        self.replay += 1
        body = (struct.pack(">BHHQ", 2, key_info, key_len, self.replay) + nonce + bytes(16) +
                bytes(8) + bytes(8) + bytes(16) + struct.pack(">H", len(key_data)) + key_data)
        e = bytearray(struct.pack(">BBH", 2, 3, len(body)) + body)
        if mic_key:
            e[81:97] = hmac.new(mic_key, bytes(e), hashlib.sha1).digest()[:16]
        return bytes(e)

    def wrap_gtk(self, gtk, keyid, rsn=b""):
        kd = rsn + bytes([0xDD, 6 + len(gtk), 0x00, 0x0F, 0xAC, 0x01, keyid & 3, 0]) + gtk
        if len(kd) % 8 or len(kd) < 16:
            kd += b"\xdd" + b"\0" * ((-len(kd) - 1) % 8)
            while len(kd) < 16:
                kd += b"\0" * 8
        return aes_key_wrap(self.ptk[16:32], kd)

    def verify_mic(self, eapol):
        e = bytearray(eapol)
        mic = bytes(e[81:97])
        e[81:97] = bytes(16)
        return hmac.new(self.ptk[:16], bytes(e), hashlib.sha1).digest()[:16] == mic

    def on_associated(self, sta, sta_idx):
        self.reset()
        self.sta, self.sta_idx = sta, sta_idx
        self.send_m1()

    def send_m1(self):
        self.m1_sent_at = time.monotonic()
        self.to_guest(ETH_EAPOL, self.eapol_key(0x008A, 16, self.anonce, b""))
        log("AP: EAPOL 1/4 sent")

    def on_eapol(self, p):
        if len(p) < 99 or p[1] != 3:
            return
        key_info, = struct.unpack_from(">H", p, 5)
        if key_info & 0x0008 and not key_info & 0x0200:          # 2/4
            snonce = p[17:49]
            a, s = self.bssid, self.sta
            self.ptk = prf_sha1(self.pmk, b"Pairwise key expansion",
                                min(a, s) + max(a, s) + min(self.anonce, snonce) + max(self.anonce, snonce), 48)
            if not check(self.verify_mic(p), "EAPOL 2/4 MIC verifies with the AP's PTK (right passphrase, PRF)"):
                return
            kd_len, = struct.unpack_from(">H", p, 97)
            check(p[99:99 + kd_len] == self.chip.assoc_ie, "EAPOL 2/4 carries the association RSN IE")
            m3 = self.eapol_key(0x13CA, 16, self.anonce, self.wrap_gtk(self.gtk, self.gtk_idx, RSN_CCMP), self.ptk[:16])
            self.to_guest(ETH_EAPOL, m3)
            log("AP: EAPOL 3/4 sent")
        elif key_info & 0x0008:                                    # 4/4
            check(self.verify_mic(p), "EAPOL 4/4 MIC verifies")
            check(key_info & 0x0200, "EAPOL 4/4 has Secure set")
            self.secure = True
            milestone("4-way handshake complete")
        else:                                                      # group 2/2
            if check(self.verify_mic(p), "group key 2/2 MIC verifies") and self.rekey_pending:
                self.rekeyed = True
                milestone("group rekey acknowledged")

    def start_rekey(self):
        self.gtk = os.urandom(16)
        self.gtk_idx = 2 if self.gtk_idx == 1 else 1
        self.rekey_pending = (self.gtk, self.gtk_idx)
        g1 = self.eapol_key(0x1382, 0, bytes(32), self.wrap_gtk(self.gtk, self.gtk_idx), self.ptk[:16])
        self.to_guest(ETH_EAPOL, g1)
        log("AP: group key 1/2 sent (GTK index %d)" % self.gtk_idx)

    # --- frames the guest transmits (802.3 out of the hostdesc)
    def on_eth(self, dst, src, etype, payload):
        if etype == ETH_EAPOL:
            self.on_eapol(payload)
            return
        if not self.chip.link_open():
            check(False, "data frame (ethertype %#06x) sent before the port was open" % etype)
            return
        if etype == ETH_ARP:
            self.on_arp(payload)
        elif etype == ETH_IP:
            self.on_ip(payload)

    def eth_to_guest(self, etype, payload):
        self.to_guest(etype, payload)

    def on_arp(self, p):
        if len(p) < 28:
            return
        op, = struct.unpack_from("!H", p, 6)
        sha, spa, tpa = p[8:14], p[14:18], p[24:28]
        if op == 1 and tpa == self.ip:
            milestone("ARP for the gateway answered")
            rep = struct.pack("!HHBBH", 1, ETH_IP, 6, 4, 2) + self.bssid + self.ip + sha + spa
            self.eth_to_guest(ETH_ARP, rep)

    def on_ip(self, p):
        if len(p) < 20:
            return
        ihl = (p[0] & 0xF) * 4
        proto, src, dst = p[9], p[12:16], p[16:20]
        body = p[ihl:]
        if proto == 17:
            sport, dport, ulen = struct.unpack_from("!HHH", body, 0)
            self.on_udp(src, dst, sport, dport, body[8:ulen])
        elif proto == 1 and dst == self.ip and body[0] == 8:          # echo request to us
            rep = bytes([0, 0, 0, 0]) + body[4:]
            rep = rep[:2] + struct.pack("!H", csum(rep)) + rep[4:]
            self.eth_to_guest(ETH_IP, ip_packet(self.ip, src, 1, rep))
            milestone("guest pinged the gateway")
        elif proto == 1 and body[0] == 0 and self.ping_id is not None:  # echo reply to our ping
            ident, = struct.unpack_from("!H", body, 4)
            if ident == self.ping_id:
                self.ping_id = None
                check(True, "guest answered the AP's ICMP echo request")
                milestone("guest answers ping")
        elif proto == 6 and dst == self.ip:
            self.on_tcp(src, body)

    # DHCP
    def on_udp(self, src, dst, sport, dport, data):
        if dport == 67 and sport == 68:
            self.on_dhcp(data)
        elif dport == 7 and dst == self.ip:
            milestone("UDP echo")
            self.eth_to_guest(ETH_IP, ip_packet(self.ip, src, 17, udp_segment(self.ip, src, 7, sport, data)))

    def on_dhcp(self, d):
        if len(d) < 240 or d[236:240] != b"\x63\x82\x53\x63":
            return
        xid, chaddr = d[4:8], d[28:34]
        opts, i, mtype = d[240:], 0, None
        while i < len(opts) and opts[i] != 255:
            if opts[i] == 0:
                i += 1
                continue
            if opts[i] == 53:
                mtype = opts[i + 2]
            i += 2 + opts[i + 1]
        check(chaddr == self.sta, "DHCP client hardware address %s is the adapter MAC %s" % (chaddr.hex(), self.sta.hex())) if mtype == 1 else None
        if mtype not in (1, 3):
            return
        reply = 2 if mtype == 1 else 5
        milestone("DHCP " + ("DISCOVER" if mtype == 1 else "REQUEST"))
        b = bytearray(240)
        b[0:4] = bytes([2, 1, 6, 0])
        b[4:8] = xid
        b[16:20] = self.lease_ip
        b[20:24] = self.ip
        b[28:34] = chaddr
        b[236:240] = b"\x63\x82\x53\x63"
        b += bytes([53, 1, reply, 54, 4]) + self.ip
        b += bytes([51, 4]) + struct.pack("!I", 3600)
        b += bytes([1, 4]) + ip4("255.255.255.0") + bytes([3, 4]) + self.ip + bytes([6, 4]) + self.ip
        b += bytes([255])
        udp = udp_segment(self.ip, ip4("255.255.255.255"), 67, 68, bytes(b))
        self.eth_to_guest(ETH_IP, ip_packet(self.ip, ip4("255.255.255.255"), 17, udp))
        if reply == 5:
            self.lease_given = True
            milestone("DHCP lease acknowledged")
            asyncio.get_event_loop().call_later(1.0, self.ping_guest)

    def ping_guest(self):
        if not self.lease_given:
            return
        self.ping_id = random.randint(1, 0xFFFF)
        icmp = struct.pack("!BBHHH", 8, 0, 0, self.ping_id, 1) + b"aic8800-sim-ping"
        icmp = icmp[:2] + struct.pack("!H", csum(icmp)) + icmp[4:]
        self.eth_to_guest(ETH_IP, ip_packet(self.ip, self.lease_ip, 1, icmp))
        log("AP: ICMP echo request -> guest")

    # A minimal TCP echo server on port 7 (enough for one short exchange).
    def on_tcp(self, src, seg):
        sport, dport, seq, ack, off, flags = struct.unpack_from("!HHIIBB", seg, 0)
        data = seg[(off >> 4) * 4:]
        key = (src, sport)
        SYN, ACK, FIN, RST, PSH = 0x02, 0x10, 0x01, 0x04, 0x08
        if dport != 7:
            self.tcp_send(src, sport, dport, 0, seq + 1, RST | ACK)
            return
        c = self.tcp.get(key)
        if flags & SYN:
            c = {"iss": random.randint(0, 0xFFFFFFF), "rcv": seq + 1}
            c["snd"] = c["iss"] + 1
            self.tcp[key] = c
            self.tcp_send(src, sport, dport, c["iss"], c["rcv"], SYN | ACK)
            milestone("TCP handshake")
            return
        if c is None:
            return
        if data and seq == c["rcv"]:
            c["rcv"] = (c["rcv"] + len(data)) & 0xFFFFFFFF
            self.tcp_send(src, sport, dport, c["snd"], c["rcv"], ACK | PSH, data)
            c["snd"] = (c["snd"] + len(data)) & 0xFFFFFFFF
            milestone("TCP echo")
        elif data:
            self.tcp_send(src, sport, dport, c["snd"], c["rcv"], ACK)
        if flags & FIN:
            c["rcv"] = (c["rcv"] + 1) & 0xFFFFFFFF
            self.tcp_send(src, sport, dport, c["snd"], c["rcv"], FIN | ACK)
            c["snd"] = (c["snd"] + 1) & 0xFFFFFFFF
        if flags & RST:
            self.tcp.pop(key, None)

    def tcp_send(self, dst, dport, sport, seq, ack, flags, data=b""):
        seg = tcp_segment(self.ip, dst, sport, dport, seq & 0xFFFFFFFF, ack & 0xFFFFFFFF, flags, data)
        self.eth_to_guest(ETH_IP, ip_packet(self.ip, dst, 6, seg))


# ----------------------------------------------------------------------------
# the chip
# ----------------------------------------------------------------------------

class Chip:
    def __init__(self, fw_dir, ssid, psk):
        self.fw_dir = fw_dir
        self.stage = "loader"
        self.mem = {}
        self.ap = AccessPoint(self, ssid, psk)
        self.port = None                # UsbRedirPort currently attached
        self.len_checked = False
        self.reset_fw()
        self.boots = 0

    def reset_fw(self):
        self.fw_cmds = []
        self.in_q = {EP_DATA_IN: [], EP_MSG_IN: []}
        self.vif_added = False
        self.sta_mac = bytes(6)
        self.assoc = None               # (bssid, ssid) while associated
        self.assoc_ie = b""
        self.open_network = False
        self.port_open = False
        self.wpa = False
        self.keys = []
        self.pn = 0
        self.scan_count = 0

    def next_pn(self):
        self.pn += 1
        return self.pn

    def link_open(self):
        return self.assoc is not None and (self.open_network or self.port_open)

    # --- memory (word granular, like the DBG commands)
    def rd(self, a):
        if a == CHIP_REV_ADDR:
            return CHIP_REV_WORD
        return self.mem.get(a, 0)

    def wr(self, a, v):
        self.mem[a] = v & 0xFFFFFFFF

    def mem_bytes(self, a, n):
        return b"".join(self.rd(a + i).to_bytes(4, "little") for i in range(0, n, 4))[:n]

    # --- queues toward the host
    def queue_msg(self, pkt):
        self.in_q[EP_MSG_IN if self.stage == "fdrv" else EP_DATA_IN].append(pkt)
        if self.port:
            self.port.pump()

    def queue_data(self, pkt):
        self.in_q[EP_DATA_IN].append(pkt)
        if self.port:
            self.port.pump()

    def take_in(self, ep, maxlen):
        """The next IN transfer on `ep`, at most `maxlen` bytes.  Data packets
        queued back to back are aggregated into one transfer, the way the
        firmware's USB RX aggregation does it."""
        q = self.in_q.get(ep)
        if not q:
            return None
        out = q.pop(0)
        if self.stage == "fdrv" and ep == EP_DATA_IN:
            while q and len(out) + len(q[0]) <= maxlen and len(out) + len(q[0]) <= 20480:
                out += q.pop(0)
        if len(out) > maxlen:
            check(False, "IN transfer of %d bytes did not fit the host's %d-byte buffer" % (len(out), maxlen))
            q.insert(0, out[maxlen:])
            out = out[:maxlen]
        return out

    # --- bulk OUT from the host
    def on_out(self, ep, buf):
        if len(buf) < 4:
            check(False, "runt OUT transfer (%d bytes) on ep %#x" % (len(buf), ep))
            return
        ln, typ = struct.unpack_from("<HB", buf, 0)
        if typ == 0x11:
            if self.stage == "fdrv" and ep != EP_MSG_OUT:
                check(False, "firmware-stage command sent on ep %#x instead of the message pipe" % ep)
            if not self.len_checked:
                self.len_checked = True
                if not check(ln == len(buf) - 4, "command length field counts the bytes after the 4-byte header"):
                    return
            mid, dest, src, plen = struct.unpack_from("<HHHH", buf, 8)
            param = bytes(buf[16:16 + plen])
            if self.stage == "loader":
                self.on_loader_cmd(mid, param)
            else:
                self.on_fw_cmd(mid, param)
        elif typ == 0x01:
            self.on_data_tx(ep, buf)
        else:
            check(False, "unknown OUT packet type %#x" % typ)

    # --- ROM loader
    def on_loader_cmd(self, mid, p):
        def cfm(param):
            self.queue_msg(cfg_packet(mid + 1, param))
        if mid == DBG_MEM_READ:
            a, = struct.unpack_from("<I", p, 0)
            cfm(struct.pack("<II", a, self.rd(a)))
        elif mid == DBG_MEM_WRITE:
            a, v = struct.unpack_from("<II", p, 0)
            self.wr(a, v)
            cfm(struct.pack("<II", a, v))
        elif mid == DBG_BLOCK_WRITE:
            a, n = struct.unpack_from("<II", p, 0)
            data = p[8:8 + n]
            if len(data) != n:
                check(False, "block write at %#x announces %d bytes, carries %d" % (a, n, len(data)))
            for i in range(0, n, 4):
                self.wr(a + i, int.from_bytes(data[i:i + 4].ljust(4, b"\0"), "little"))
            if a == FMAC_ADDR:
                milestone("firmware download started")
            cfm(struct.pack("<II", a, n))
        elif mid == DBG_START_APP:
            boot, typ = struct.unpack_from("<II", p, 0)
            check((boot, typ) == (FMAC_ADDR, 1), "DBG_START_APP(0x120000, AUTO) starts the firmware")
            self.verify_images()
            milestone("firmware started")
            self.boots += 1
            self.reenumerate("fdrv", 0.3)
        else:
            check(False, "unexpected loader command %#x" % mid)

    def verify_images(self):
        def load(name):
            try:
                with open(os.path.join(self.fw_dir, name), "rb") as f:
                    return f.read()
            except OSError:
                return None
        fmac = load("fmacfw_8800d80_u02.bin")
        if fmac:
            # The patch table writes land inside the image; compare outside it.
            check(self.mem_bytes(FMAC_ADDR, 0x190) == fmac[:0x190] and
                  self.mem_bytes(FMAC_ADDR + 0x400, 0x8000) == fmac[0x400:0x8400],
                  "fmacfw image is in chip memory at 0x120000")
        adid = load("fw_adid_8800d80_u02.bin")
        if adid:
            check(self.mem_bytes(ADID_ADDR, 64) == adid[:64], "adid patch is in chip memory at 0x201940")
        # fw_patch_table names the load addresses: the patch at 0x1e0000 and
        # its ext0 image at 0x20b43c for this firmware release.
        patch = load("fw_patch_8800d80_u02.bin")
        if patch:
            check(self.mem_bytes(0x1E0000, 64) == patch[:64], "fw patch is in chip memory at 0x1e0000")
        ext0 = load("fw_patch_8800d80_u02_ext0.bin")
        if ext0:
            check(self.mem_bytes(PATCH_ADDR, 64) == ext0[:64], "fw patch ext0 is in chip memory at 0x20b43c")

    # --- firmware
    def on_fw_cmd(self, mid, p):
        name = MSG_NAMES.get(mid, "%#06x" % mid)
        vlog("fw cmd", name, len(p), "bytes")
        self.fw_cmds.append(mid)

        def cfm(param=b""):
            self.queue_msg(cfg_packet(mid + 1, param))

        if mid == DBG_START_APP:
            boot, typ = struct.unpack_from("<II", p, 0)
            check(typ == 3, "firmware-stage DBG_START_APP is a REBOOT request")
            milestone("warm reboot requested")
            self.reenumerate("loader", min(max(boot, 200), 3000) / 1000.0)
            return
        if mid == DBG_MEM_READ:
            a, = struct.unpack_from("<I", p, 0)
            cfm(struct.pack("<II", a, self.rd(a)))
        elif mid == DBG_MEM_WRITE:
            a, v = struct.unpack_from("<II", p, 0)
            self.wr(a, v)
            cfm(struct.pack("<II", a, v))
        elif mid == MM_STACK_START:
            cfm(bytes([1, 0]))                                   # 5 GHz capable
        elif mid == MM_TXPWR:
            cfm(b"\0\0\0\0")
        elif mid == MM_RF_CALIB:
            cfm(b"\0" * 16)
        elif mid == MM_GET_MAC:
            cfm(bytes.fromhex("88001e8d8000"))
        elif mid == MM_VERSION:
            cfm(struct.pack("<IIII", 0x06070000, 0, 0, 0))
        elif mid == ME_CONFIG:
            check(len(p) == 112 and p[103] == 1, "ME_CONFIG is 112 bytes with HT supported")
            cfm()
        elif mid == ME_CHAN_CONFIG:
            ok = len(p) == 254 and p[252] == 14 and struct.unpack_from("<H", p, 0)[0] == 2412
            check(ok, "ME_CHAN_CONFIG lists 14 2.4 GHz channels from 2412 MHz")
            cfm()
        elif mid in (MM_RESET, MM_START, MM_SET_COEX, MM_SET_PS):
            cfm()
        elif mid == MM_ADD_IF:
            got = [m for m in self.fw_cmds if m in VENDOR_BRING_ORDER]
            check(got == VENDOR_BRING_ORDER, "bring-up follows the vendor order: " +
                  " ".join(MSG_NAMES.get(m, hex(m)) for m in got))
            self.vif_added = True
            self.sta_mac = bytes(p[2:8])     # mac_addr is u16-aligned: one pad byte
            check(self.sta_mac[0] & 1 == 0 and self.sta_mac != bytes(6), "MM_ADD_IF carries a unicast station MAC")
            cfm(bytes([0, 0]))
            milestone("station interface up")
        elif mid == SCANU_START:
            self.on_scan(p)
        elif mid == SM_CONNECT:
            self.on_connect(p)
        elif mid == SM_DISCONNECT:
            cfm()
            if self.assoc:
                log("guest disconnected")
            self.assoc = None
            self.port_open = False
        elif mid == MM_KEY_ADD:
            self.on_key_add(p)
            cfm(bytes([0, len(self.keys) + 4]))
        elif mid == MM_KEY_DEL:
            cfm()
        elif mid == ME_CONTROL_PORT:
            sta, state = p[0], p[1]
            check(self.wpa and self.ap.keys_installed, "ME_SET_CONTROL_PORT only after the pairwise key")
            self.port_open = bool(state)
            cfm()
            if self.port_open:
                milestone("controlled port open")
        else:
            log("unhandled firmware command %s (%d bytes) -- empty confirm" % (name, len(p)))
            cfm()

    def on_scan(self, p):
        if not check(self.vif_added, "scan only after MM_ADD_IF"):
            return
        check(len(p) >= 376 and p[352:358] == b"\xff" * 6, "SCANU_START bssid filter is broadcast")
        check(len(p) >= 376 and p[368] >= 1, "SCANU_START asks for at least one (wildcard) SSID")
        self.scan_count += 1
        self.queue_msg(cfg_packet(SCANU_CFM_ADDITIONAL))

        def beacon(bssid, ssid, rsn, freq, rssi):
            fr = bytearray(b"\x80\x00\x00\x00" + b"\xff" * 6 + bssid + bssid + b"\x00\x00")
            fr += struct.pack("<QHH", int(time.monotonic() * 1e6), 100, 0x0411 if rsn else 0x0401)
            fr += bytes([0, len(ssid)]) + ssid + bytes([1, 8, 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24])
            fr += bytes([3, 1, (freq - 2407) // 5 if freq < 5000 else (freq - 5000) // 5])
            if rsn:
                fr += rsn
            hdr = struct.pack("<HHHBBBbH", len(fr), 0x80, freq, 1 if freq > 5000 else 0, 0xFF, 0, rssi, 0)
            return cfg_packet(SCANU_RESULT_IND, hdr + bytes(fr))

        results = [beacon(self.ap.bssid, self.ap.ssid, RSN_CCMP, self.ap.freq, -38),
                   beacon(bytes.fromhex("02a1c8800d81"), b"ImplusSim-Open", None, 5180, -55),
                   beacon(bytes.fromhex("02a1c8800d82"), b"Legacy-TKIP-Group", RSN_TKIP_GROUP, 2412, -71),
                   beacon(bytes.fromhex("02a1c8800d83"), b"WPA3-Only", RSN_SAE_ONLY, 2462, -60)]

        def deliver():
            # The firmware packs several indications into one transfer.
            self.queue_msg(results[0] + results[1])
            self.queue_msg(results[2] + results[3])
            self.queue_msg(cfg_packet(SCANU_START_CFM, bytes([0, 0, 4])))
            milestone("scan answered")
        asyncio.get_event_loop().call_later(0.4, deliver)

    def on_connect(self, p):
        ssid = p[1:1 + p[0]]
        flags, = struct.unpack_from("<I", p, 48)
        ie_len, = struct.unpack_from("<H", p, 54)
        ie = p[64:64 + ie_len]
        self.queue_msg(cfg_packet(SM_CONNECT + 1, bytes([0])))
        open_net = ssid == b"ImplusSim-Open"
        if ssid != self.ap.ssid and not open_net:
            check(False, "connect to a network the simulator does not have: %r" % ssid)
            self.queue_msg(cfg_packet(SM_CONNECT_IND, struct.pack("<H", 1) + bytes(850)))
            return
        if open_net:
            check(flags == 0 and ie_len == 0, "open network: no control-port flags, no RSN IE")
        else:
            check(flags == 0x9, "WPA2 connect flags = CONTROL_PORT_HOST | WPA_WPA2_IN_USE")
            check(ie[:2] == b"\x30\x14" and ie[2:20] == RSN_CCMP[2:20],
                  "association RSN IE: CCMP/CCMP/PSK")
        self.assoc_ie = ie
        self.open_network = open_net
        self.wpa = not open_net
        self.port_open = False
        self.ap.keys_installed = False
        sta_idx = 1
        ind = bytearray(852)
        struct.pack_into("<H", ind, 0, 0)
        bssid = self.ap.bssid if not open_net else bytes.fromhex("02a1c8800d81")
        ind[2:8] = bssid
        ind[8] = 0          # roamed
        ind[9] = 0          # vif
        ind[10] = sta_idx   # ap_idx
        ind[11] = 6         # ch_idx

        def associate():
            self.assoc = (bssid, ssid)
            self.queue_msg(cfg_packet(SM_CONNECT_IND, bytes(ind)))
            milestone("associated (%s)" % ssid.decode())
            if not open_net:
                self.ap.on_associated(self.sta_mac, sta_idx)
        asyncio.get_event_loop().call_later(0.2, associate)

    def on_key_add(self, p):
        key_idx, sta_idx = p[0], p[1]
        key_len = p[4]
        key = p[8:8 + key_len]
        cipher, pairwise = p[40], p[43]
        self.keys.append((key_idx, sta_idx, key, cipher, pairwise))
        if pairwise:
            ok = self.ap.ptk is not None and key == self.ap.ptk[32:48]
            check(ok, "pairwise key installed = TK of the AP's PTK")
            self.ap.keys_installed = ok
        else:
            want = self.ap.rekey_pending[0] if self.ap.rekey_pending else self.ap.gtk
            check(key == want, "group key installed = the AP's GTK (index %d)" % key_idx)

    def on_data_tx(self, ep, buf):
        if self.stage != "fdrv":
            check(False, "DATA packet in the loader stage")
            return
        check(ep == EP_DATA_OUT, "data goes out on the data pipe") if not hasattr(self, "_data_ep_checked") else None
        self._data_ep_checked = True
        total, = struct.unpack_from("<H", buf, 0)
        total &= 0x0FFF
        plen, = struct.unpack_from("<H", buf, 4)
        if total != 4 + 28 + plen or len(buf) not in (total, total + 1):
            check(False, "DATA length field %d does not match hostdesc+payload %d (transfer %d)" %
                  (total, 4 + 28 + plen, len(buf)))
            return
        dst, src, etype = buf[12:18], buf[18:24], struct.unpack_from(">H", buf, 24)[0]
        payload = bytes(buf[32:32 + plen])
        if bytes(src) != self.sta_mac:
            check(False, "frame source %s is not the station MAC" % bytes(src).hex())
        if not self.assoc:
            check(False, "data frame sent while not associated")
            return
        if self.open_network:
            if etype == ETH_IP and len(payload) > 20 and payload[9] == 17:
                pass
            return
        self.ap.on_eth(dst, src, etype, payload)

    # --- (re)enumeration
    def reenumerate(self, stage, delay):
        async def go():
            await asyncio.sleep(delay)
            if self.port:
                await self.port.disconnect_device()
            self.stage = stage
            self.reset_fw()
            await asyncio.sleep(0.2)
            if self.port:
                await self.port.connect_device()
        asyncio.ensure_future(go())


# ----------------------------------------------------------------------------
# one QEMU connection
# ----------------------------------------------------------------------------

class UsbRedirPort:
    def __init__(self, chip, reader, writer):
        self.chip = chip
        self.r, self.w = reader, writer
        self.peer_caps = 0
        self.ids64 = False
        self.connected = False
        self.disc_ack = None
        self.pending_in = {}            # ep -> [(id, length)]
        self.guest_mac = None
        self.config = 0

    @property
    def pers(self):
        return LOADER if self.chip.stage == "loader" else FDRV

    # --- framing
    # Until both hellos have been seen the header carries a 32-bit id; after
    # that a 64-bit one when both sides have 64bits_ids.
    def send(self, typ, pid, hdr=b"", data=b""):
        if self.ids64:
            h = struct.pack("<IIQ", typ, len(hdr) + len(data), pid)
        else:
            h = struct.pack("<III", typ, len(hdr) + len(data), pid & 0xFFFFFFFF)
        self.w.write(h + hdr + data)

    async def read_packet(self):
        if self.ids64:
            typ, length, pid = struct.unpack("<IIQ", await self.r.readexactly(16))
        else:
            typ, length, pid = struct.unpack("<III", await self.r.readexactly(12))
        body = await self.r.readexactly(length) if length else b""
        return typ, pid, body

    def bulk_hdr(self, ep, status, length):
        return struct.pack("<BBHIH", ep, status, length & 0xFFFF, 0, length >> 16)

    def parse_bulk_hdr(self, body):
        ep, st, lo, _stream, hi = struct.unpack_from("<BBHIH", body, 0)
        return ep, lo | (hi << 16), body[10:]

    # --- device presence
    def send_iface_ep_info(self):
        ifi = struct.pack("<I", 1) + bytes([0] + [0] * 31) + bytes([0xFF] + [0] * 31) * 3
        types = [EP_INVALID] * 32
        mps = [0] * 32
        types[0] = types[0x10] = EP_CONTROL
        mps[0] = mps[0x10] = 64
        for a, m in self.pers.endpoints:
            types[ep_index(a)] = EP_BULK
            mps[ep_index(a)] = m
        epi = bytes(types) + bytes(32) + bytes(32) + struct.pack("<32H", *mps)
        self.send(R_INTERFACE_INFO, 0, ifi)
        self.send(R_EP_INFO, 0, epi)

    async def connect_device(self):
        self.pending_in = {}
        self.config = 1
        self.send_iface_ep_info()
        hdr = struct.pack("<BBBBHHH", SPEED_HIGH, 0, 0, 0, VID, self.pers.pid, 0x0100)
        self.send(R_DEVICE_CONNECT, 0, hdr)
        self.connected = True
        await self.w.drain()
        log("USB: device connected as %04x:%04x (%s stage)" % (VID, self.pers.pid, self.chip.stage))

    async def disconnect_device(self):
        if not self.connected:
            return
        self.connected = False
        self.pending_in = {}
        self.disc_ack = asyncio.get_event_loop().create_future()
        self.send(R_DEVICE_DISCONNECT, 0)
        await self.w.drain()
        log("USB: device disconnected (re-enumerating)")
        if self.peer_caps & CAP_DEVICE_DISCONNECT_ACK:
            try:
                await asyncio.wait_for(self.disc_ack, 5.0)
            except asyncio.TimeoutError:
                log("USB: no disconnect ack from QEMU")

    # --- IN completion
    def pump(self):
        if not self.connected:
            return
        for ep, waiters in list(self.pending_in.items()):
            while waiters:
                pid, length = waiters[0]
                data = self.chip.take_in(ep, length)
                if data is None:
                    break
                waiters.pop(0)
                if VERBOSE or (TRACE_DATA and self.chip.stage == "fdrv"):
                    log("IN  ep %#x: %d bytes -> request of %d" % (ep, len(data), length))
                self.send(R_BULK_PACKET, pid, self.bulk_hdr(ep, ST_SUCCESS, len(data)), data)

    # --- dispatch
    def on_control(self, pid, body):
        ep, req, rtype, _st, value, index, length = struct.unpack_from("<BBBBHHH", body, 0)
        data_out = body[10:]
        reply, status = b"", ST_SUCCESS
        if rtype == 0x80 and req == 6:                       # GET_DESCRIPTOR
            dtype, didx = value >> 8, value & 0xFF
            if dtype == 1:
                reply = self.pers.device
            elif dtype == 2:
                reply = self.pers.config
            elif dtype == 3 and didx in self.pers.strings:
                reply = self.pers.strings[didx]
            elif dtype == 6:                                 # device qualifier
                reply = struct.pack("<BBHBBBBBB", 10, 6, 0x0200, 0, 0, 0, 64, 1, 0)
            else:
                status = ST_STALL
        elif rtype == 0x80 and req == 0:                     # GET_STATUS
            reply = b"\x00\x00"
        elif rtype in (0x81, 0x82) and req == 0:
            reply = b"\x00\x00"
        elif rtype == 0x80 and req == 8:
            reply = bytes([self.config])
        elif rtype & 0x80 == 0:                              # SET_FEATURE / CLEAR_FEATURE / vendor OUT
            pass
        else:
            status = ST_STALL
        reply = reply[:length]
        vlog("ctrl rt=%02x req=%02x val=%04x idx=%04x len=%d -> %d bytes st=%d" %
             (rtype, req, value, index, length, len(reply), status))
        hdr = struct.pack("<BBBBHHH", ep, req, rtype, status, value, index,
                          len(reply) if rtype & 0x80 else len(data_out))
        self.send(R_CONTROL_PACKET, pid, hdr, reply if rtype & 0x80 else b"")

    def on_bulk(self, pid, body):
        ep, length, data = self.parse_bulk_hdr(body)
        if not self.connected:
            self.send(R_BULK_PACKET, pid, self.bulk_hdr(ep, ST_IOERROR, 0))
            return
        if ep & 0x80:
            if ep not in [a for a, _ in self.pers.endpoints]:
                self.send(R_BULK_PACKET, pid, self.bulk_hdr(ep, ST_STALL, 0))
                return
            self.pending_in.setdefault(ep, []).append((pid, length))
            if VERBOSE or (TRACE_DATA and self.chip.stage == "fdrv"):
                log("IN  ep %#x: request %d bytes (%d outstanding, %d queued)" %
                    (ep, length, len(self.pending_in[ep]), len(self.chip.in_q.get(ep, []))))
            self.pump()
        else:
            self.send(R_BULK_PACKET, pid, self.bulk_hdr(ep, ST_SUCCESS, len(data)))
            try:
                self.chip.on_out(ep, data)
            except Exception as e:  # keep the device alive; report the bug
                import traceback
                traceback.print_exc()
                check(False, "simulator exception on OUT ep %#x: %s" % (ep, e))

    def on_cancel(self, pid):
        for ep, waiters in self.pending_in.items():
            for i, (wid, _) in enumerate(waiters):
                if wid == pid:
                    waiters.pop(i)
                    self.send(R_BULK_PACKET, pid, self.bulk_hdr(ep, ST_CANCELLED, 0))
                    if VERBOSE or TRACE_DATA:
                        log("IN request on ep %#x cancelled" % ep)
                    return

    async def run(self):
        hello = b"aic8800-sim 1.0".ljust(64, b"\0")
        self.send(R_HELLO, 0, hello, struct.pack("<I", OUR_CAPS))
        await self.w.drain()
        while True:
            typ, pid, body = await self.read_packet()
            if typ not in (R_CONTROL_PACKET, R_BULK_PACKET):
                vlog("usbredir <- type %d id %d %s" % (typ, pid, body[:16].hex()))
            if typ == R_HELLO:
                caps = body[64:]
                self.peer_caps = struct.unpack_from("<I", caps, 0)[0] if len(caps) >= 4 else 0
                self.ids64 = bool(self.peer_caps & OUR_CAPS & CAP_64BITS_IDS)
                if not self.peer_caps & CAP_32BITS_BULK_LENGTH:
                    check(False, "QEMU lacks 32bits_bulk_length; this simulator needs it")
                log("usbredir: peer '%s' caps=%#x" % (body[:64].rstrip(b"\0").decode(errors="replace"), self.peer_caps))
                # Plug the device in a moment after the handshake, not in the
                # same breath: attaching it while QEMU is still bringing the
                # machine up has been seen to wedge the boot.
                asyncio.get_event_loop().call_later(CONNECT_DELAY, lambda: asyncio.ensure_future(self.connect_device()))
            elif typ == R_CONTROL_PACKET:
                self.on_control(pid, body)
            elif typ == R_BULK_PACKET:
                self.on_bulk(pid, body)
            elif typ == R_CANCEL_DATA_PACKET:
                self.on_cancel(pid)
            elif typ == R_SET_CONFIGURATION:
                self.config = body[0]
                self.send_iface_ep_info()
                self.send(R_CONFIGURATION_STATUS, pid, bytes([ST_SUCCESS, self.config]))
            elif typ == R_GET_CONFIGURATION:
                self.send(R_CONFIGURATION_STATUS, pid, bytes([ST_SUCCESS, self.config]))
            elif typ == R_SET_ALT_SETTING:
                self.send_iface_ep_info()
                self.send(R_ALT_SETTING_STATUS, pid, bytes([ST_SUCCESS, body[0], body[1]]))
            elif typ == R_GET_ALT_SETTING:
                self.send(R_ALT_SETTING_STATUS, pid, bytes([ST_SUCCESS, body[0], 0]))
            elif typ == R_DEVICE_DISCONNECT_ACK:
                if self.disc_ack and not self.disc_ack.done():
                    self.disc_ack.set_result(True)
            elif typ == R_RESET:
                vlog("usbredir: port reset")
            elif typ in (R_FILTER_FILTER, R_FILTER_REJECT, R_START_BULK_RECV, R_STOP_BULK_RECV,
                         R_START_INTR, R_STOP_INTR):
                vlog("usbredir: ignoring packet type", typ)
            else:
                log("usbredir: unhandled packet type %d (%d bytes)" % (typ, len(body)))
            await self.w.drain()


# ----------------------------------------------------------------------------
# scenario driver: what the AP does once the guest is on the network
# ----------------------------------------------------------------------------

async def ap_supervisor(chip, rekey_after):
    rekey_started = False
    m1_retries = 0
    while True:
        await asyncio.sleep(0.5)
        ap = chip.ap
        # Retransmit message 1 like hostapd does if the station is silent.
        if chip.assoc and chip.wpa and ap.ptk is None and ap.m1_sent_at and \
                time.monotonic() - ap.m1_sent_at > 2.0 and m1_retries < 3:
            m1_retries += 1
            ap.send_m1()
        if chip.assoc is None:
            rekey_started = False
            m1_retries = 0
        if ap.lease_given and ap.secure and not rekey_started and rekey_after >= 0 and \
                "guest answers ping" in EVENTS:
            await asyncio.sleep(rekey_after)
            rekey_started = True
            ap.start_rekey()


def summary(report_path):
    fails = [t for ok, t in CHECKS if not ok]
    print("\n==== AIC8800 simulator summary ====")
    print("milestones: " + " -> ".join(EVENTS))
    print("checks: %d passed, %d failed" % (len(CHECKS) - len(fails), len(fails)))
    for t in fails:
        print("  FAIL " + t)
    if report_path:
        with open(report_path, "w") as f:
            json.dump({"milestones": EVENTS, "checks": [{"ok": ok, "text": t} for ok, t in CHECKS]}, f, indent=1)


async def main():
    global VERBOSE, TRACE_DATA, CONNECT_DELAY
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=7788)
    ap.add_argument("--fw-dir", required=True)
    ap.add_argument("--ssid", default="ImplusSim")
    ap.add_argument("--psk", default="sim-passphrase-8800")
    ap.add_argument("--stage", choices=["loader", "fdrv"], default="loader",
                    help="stage the chip is in when QEMU first connects (fdrv = warm boot)")
    ap.add_argument("--rekey-after", type=float, default=2.0,
                    help="seconds after the first successful ping to rekey the group key (-1: never)")
    ap.add_argument("--report", help="write milestones/checks as JSON here")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--connect-delay", type=float, default=2.0,
                    help="seconds between the usbredir handshake and plugging the device in")
    ap.add_argument("--trace-data", action="store_true",
                    help="log firmware-stage IN requests/completions (cheap, unlike -v)")
    args = ap.parse_args()
    VERBOSE = args.verbose
    TRACE_DATA = args.trace_data
    CONNECT_DELAY = args.connect_delay

    chip = Chip(args.fw_dir, args.ssid, args.psk)
    chip.stage = args.stage

    async def on_client(reader, writer):
        if chip.port is not None:
            log("second connection refused")
            writer.close()
            return
        log("QEMU connected (chip in %s stage)" % chip.stage)
        port = UsbRedirPort(chip, reader, writer)
        chip.port = port
        try:
            await port.run()
        except (asyncio.IncompleteReadError, ConnectionResetError):
            log("QEMU disconnected")
        finally:
            chip.port = None
            # The chip keeps running (its firmware stays loaded) across a
            # QEMU restart, exactly what a warm reboot of the host does.
            chip.in_q = {EP_DATA_IN: [], EP_MSG_IN: []}
            chip.assoc = None
            summary(args.report)

    server = await asyncio.start_server(on_client, "127.0.0.1", args.port)
    log("listening on 127.0.0.1:%d (chip in %s stage)" % (args.port, chip.stage))
    asyncio.ensure_future(ap_supervisor(chip, args.rekey_after))
    stop = asyncio.get_event_loop().create_future()
    for s in (signal.SIGINT, signal.SIGTERM):
        asyncio.get_event_loop().add_signal_handler(s, lambda: stop.done() or stop.set_result(True))
    await stop
    server.close()
    summary(args.report)


if __name__ == "__main__":
    asyncio.run(main())
