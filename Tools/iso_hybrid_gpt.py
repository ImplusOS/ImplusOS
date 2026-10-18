#!/usr/bin/env python3
"""iso_hybrid_gpt.py -- make an El Torito (UEFI) ISO bootable from a USB stick.

Why this exists
---------------
A UEFI-only El Torito ISO (genisoimage ... -eltorito-alt-boot -e esp.img) boots
from optical media, because the firmware reads the boot catalog.  A USB stick
has no boot catalog: UEFI firmware looks for a GPT (or MBR) partition of type
"EFI System" and runs \\EFI\\BOOT\\BOOT<ARCH>.EFI from its FAT filesystem.  A
bare ISO written to a stick has no partition table at all, so the firmware
finds nothing.  (syslinux `isohybrid --uefi` cannot help: it needs an isolinux
boot image and fails on a UEFI-only disc.)

What it does
------------
Given a finished ISO it, without touching any ISO9660/UDF structure beyond the
32 KiB system area:

  1. writes a protective MBR (sector 0) and a primary GPT (sectors 1..33)
     into the ISO system area, which ISO9660 leaves unused;
  2. appends a copy of the ISO's own esp.img at a 1 MiB boundary after the
     end of the ISO, followed by the backup GPT in the last sectors;
  3. declares two non-overlapping partitions:
        1  "ImplusOS ISO"  Microsoft basic data   (the ISO9660/UDF volume)
        2  "ImplusOS ESP"  EFI System Partition   (the appended esp.img copy)

Result: the same file is still a valid optical image (El Torito, ISO9660 +
UDF, unchanged offsets -- the volume is NOT shifted) and also a valid GPT disk
image for `dd`.  The ESP is a separate, spec-clean partition rather than an
overlapping one, which is what picky firmware validators prefer.

It also sanity-checks what UEFI firmware cares about: the FAT type implied by
the cluster count (a "FAT32" volume with fewer than 65525 clusters is read as
FAT16 by many drivers), the 0x55AA boot-sector signature, and that the El
Torito catalog carries an EFI (0xEF) entry.

Limitations
-----------
 * Geometry is 512-byte logical sectors (nearly all USB sticks).  A 4Kn device
   would need an image built for 4096-byte sectors.
 * Running it twice is safe: a previous graft is detected and replaced.

Usage
-----
    iso_hybrid_gpt.py IMAGE.iso              graft, then verify
    iso_hybrid_gpt.py --verify IMAGE.iso     verify only (exit 1 on error)
    iso_hybrid_gpt.py --esp-name esp.img ... name of the ESP file in the ISO
"""

import argparse
import hashlib
import struct
import sys
import uuid
import zlib

SECTOR = 512
ISO_BLOCK = 2048
SYSTEM_AREA = 32768
ISO_FIRST_LBA = SYSTEM_AREA // SECTOR            # 64
GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_SECTORS = GPT_ENTRIES * GPT_ENTRY_SIZE // SECTOR   # 32
ALIGN_SECTORS = 2048                              # 1 MiB

TYPE_ESP = uuid.UUID("C12A7328-F81F-11D2-BA4B-00A0C93EC93B")
TYPE_BASIC = uuid.UUID("EBD0A0A2-B9E5-4433-87C0-68B6B72699C7")
GUID_NS = uuid.UUID("6f1b8f3e-5c0d-4a0e-9d6b-1d0a5b7e2c11")
NAME_ISO = "ImplusOS ISO"
NAME_ESP = "ImplusOS ESP"


class ToolError(Exception):
    pass


# ── ISO9660 / El Torito ────────────────────────────────────────────────────

def read_pvd(f):
    f.seek(16 * ISO_BLOCK)
    pvd = f.read(ISO_BLOCK)
    if len(pvd) != ISO_BLOCK or pvd[0] != 1 or pvd[1:6] != b"CD001":
        raise ToolError("no ISO9660 primary volume descriptor at sector 16")
    vol_blocks = struct.unpack_from("<I", pvd, 80)[0]
    block_size = struct.unpack_from("<H", pvd, 128)[0]
    if block_size != ISO_BLOCK:
        raise ToolError("unsupported ISO logical block size %d" % block_size)
    # root directory record lives at PVD offset 156: extent at +2, length at +10
    root_lba = struct.unpack_from("<I", pvd, 156 + 2)[0]
    root_len = struct.unpack_from("<I", pvd, 156 + 10)[0]
    created = pvd[813:830]
    return vol_blocks * block_size, root_lba, root_len, created


def iso_find(f, dir_lba, dir_len, wanted):
    """Return (lba, size) of a regular file in one directory, or None."""
    f.seek(dir_lba * ISO_BLOCK)
    data = f.read(dir_len)
    pos = 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:                      # padding up to the next block
            pos = (pos // ISO_BLOCK + 1) * ISO_BLOCK
            continue
        lba = struct.unpack_from("<I", data, pos + 2)[0]
        size = struct.unpack_from("<I", data, pos + 10)[0]
        flags = data[pos + 25]
        id_len = data[pos + 32]
        ident = data[pos + 33:pos + 33 + id_len]
        pos += rec_len
        if id_len == 1 and ident in (b"\x00", b"\x01"):
            continue
        name = ident.decode("latin-1").split(";")[0].rstrip(".").upper()
        if name == wanted.upper() and not flags & 2:
            return lba, size
    return None


def read_eltorito(f):
    """Return a list of (platform_id, load_rba) boot entries, or []."""
    data = b""
    for sector in range(16, 16 + 64):
        f.seek(sector * ISO_BLOCK)
        vd = f.read(ISO_BLOCK)
        if len(vd) < ISO_BLOCK or vd[0] == 255:
            break
        if vd[0] == 0 and vd[7:30] == b"EL TORITO SPECIFICATION":
            cat = struct.unpack_from("<I", vd, 71)[0]
            f.seek(cat * ISO_BLOCK)
            data = f.read(ISO_BLOCK)
            break
    if len(data) < 64 or data[0] != 1 or data[30:32] != b"\x55\xaa":
        return []
    entries = []
    platform = data[1]
    pos = 32
    if data[pos] == 0x88:
        entries.append((platform, struct.unpack_from("<I", data, pos + 8)[0]))
    pos += 32
    while pos + 32 <= len(data) and data[pos] in (0x90, 0x91):
        platform = data[pos + 1]
        count = struct.unpack_from("<H", data, pos + 2)[0]
        final = data[pos] == 0x91
        pos += 32
        for _ in range(count):
            if pos + 32 > len(data):
                break
            if data[pos] == 0x88:
                entries.append(
                    (platform, struct.unpack_from("<I", data, pos + 8)[0]))
            pos += 32
        if final:
            break
    return entries


# ── FAT sanity ─────────────────────────────────────────────────────────────

def fat_check(buf, what):
    """Return (kind, errors, warnings) for a FAT boot sector image."""
    errs, warns = [], []
    if len(buf) < SECTOR or buf[510:512] != b"\x55\xaa":
        return "?", ["%s: missing 0x55AA boot-sector signature" % what], warns
    bps, spc, rsvd, nfats, root_ent, tot16 = struct.unpack_from(
        "<HBHBHH", buf, 11)
    fatsz16 = struct.unpack_from("<H", buf, 22)[0]
    tot32 = struct.unpack_from("<I", buf, 32)[0]
    fatsz32 = struct.unpack_from("<I", buf, 36)[0]
    if bps not in (512, 1024, 2048, 4096) or spc == 0 or spc & (spc - 1) \
            or nfats == 0 or rsvd == 0:
        return "?", ["%s: not a valid FAT BIOS parameter block" % what], warns
    tot = tot16 or tot32
    fatsz = fatsz16 or fatsz32
    root_secs = (root_ent * 32 + bps - 1) // bps
    data_secs = tot - (rsvd + nfats * fatsz + root_secs)
    clusters = data_secs // spc
    if clusters < 4085:
        kind = "FAT12"
    elif clusters < 65525:
        kind = "FAT16"
    else:
        kind = "FAT32"
    fat32_layout = fatsz16 == 0
    if fat32_layout and kind != "FAT32":
        errs.append("%s: formatted as FAT32 but has only %d clusters (<65525); "
                    "UEFI FAT drivers will read it as %s -- make the ESP "
                    "larger or format it as FAT16" % (what, clusters, kind))
    if not fat32_layout and kind == "FAT32":
        errs.append("%s: FAT12/16 layout with %d clusters (>=65525)"
                    % (what, clusters))
    if tot * bps > len(buf):
        errs.append("%s: BPB says %d bytes but the image holds %d"
                    % (what, tot * bps, len(buf)))
    if bps != SECTOR:
        warns.append("%s: FAT sector size is %d, not 512" % (what, bps))
    return kind, errs, warns


# ── GPT ────────────────────────────────────────────────────────────────────

def gpt_entry(type_guid, unique, first, last, name):
    e = bytearray(GPT_ENTRY_SIZE)
    e[0:16] = type_guid.bytes_le
    e[16:32] = unique.bytes_le
    struct.pack_into("<QQQ", e, 32, first, last, 0)
    e[56:128] = name.encode("utf-16-le").ljust(72, b"\0")[:72]
    return bytes(e)


def gpt_header(cur, backup, first_usable, last_usable, disk_guid,
               entries_lba, entries_crc):
    h = bytearray(92)
    h[0:8] = b"EFI PART"
    struct.pack_into("<IIII", h, 8, 0x00010000, 92, 0, 0)
    struct.pack_into("<QQQQ", h, 24, cur, backup, first_usable, last_usable)
    h[56:72] = disk_guid.bytes_le
    struct.pack_into("<QIII", h, 72, entries_lba, GPT_ENTRIES,
                     GPT_ENTRY_SIZE, entries_crc)
    struct.pack_into("<I", h, 16, zlib.crc32(bytes(h)) & 0xFFFFFFFF)
    return bytes(h).ljust(SECTOR, b"\0")


def parse_gpt_header(sector):
    if sector[0:8] != b"EFI PART":
        return None
    size = struct.unpack_from("<I", sector, 12)[0]
    if not 92 <= size <= SECTOR:
        return None
    raw = bytearray(sector[:size])
    stored = struct.unpack_from("<I", raw, 16)[0]
    struct.pack_into("<I", raw, 16, 0)
    if zlib.crc32(bytes(raw)) & 0xFFFFFFFF != stored:
        return None
    cur, backup, first, last = struct.unpack_from("<QQQQ", sector, 24)
    entries_lba, num, esize, ecrc = struct.unpack_from("<QIII", sector, 72)
    return dict(cur=cur, backup=backup, first=first, last=last,
                entries_lba=entries_lba, num=num, esize=esize, ecrc=ecrc,
                guid=uuid.UUID(bytes_le=sector[56:72]))


def read_partitions(f, hdr):
    f.seek(hdr["entries_lba"] * SECTOR)
    blob = f.read(hdr["num"] * hdr["esize"])
    ok = zlib.crc32(blob) & 0xFFFFFFFF == hdr["ecrc"]
    parts = []
    for i in range(hdr["num"]):
        e = blob[i * hdr["esize"]:(i + 1) * hdr["esize"]]
        if len(e) < 128 or e[0:16] == bytes(16):
            continue
        first, last, _ = struct.unpack_from("<QQQ", e, 32)
        name = e[56:128].decode("utf-16-le", "replace").rstrip("\0")
        parts.append(dict(index=i + 1, type=uuid.UUID(bytes_le=e[0:16]),
                          first=first, last=last, name=name))
    return parts, ok


def previous_graft_end(f):
    """Byte size of the ISO part if this tool already grafted the file."""
    f.seek(SECTOR)
    hdr = parse_gpt_header(f.read(SECTOR))
    if not hdr:
        return None
    parts, _ = read_partitions(f, hdr)
    for p in parts:
        if p["name"] == NAME_ISO and p["first"] == ISO_FIRST_LBA:
            return (p["last"] + 1) * SECTOR
    return None


# ── graft ──────────────────────────────────────────────────────────────────

def align_up(n, a):
    return (n + a - 1) // a * a


def graft(path, esp_name, force):
    with open(path, "r+b") as f:
        prev = previous_graft_end(f)
        if prev:
            f.truncate(prev)
            f.seek(446)
            f.write(bytes(64))
            f.seek(SECTOR)
            f.write(bytes(SYSTEM_AREA - SECTOR))
        size = f.seek(0, 2)
        pvd_bytes, root_lba, root_len, created = read_pvd(f)
        if size < pvd_bytes:
            raise ToolError("file (%d bytes) is shorter than the ISO volume "
                            "(%d bytes)" % (size, pvd_bytes))
        if size % ISO_BLOCK:
            f.write(bytes(align_up(size, ISO_BLOCK) - size))
            size = align_up(size, ISO_BLOCK)
        found = iso_find(f, root_lba, root_len, esp_name)
        if not found:
            raise ToolError("'%s' not found in the ISO root directory"
                            % esp_name)
        esp_lba, esp_len = found
        f.seek(esp_lba * ISO_BLOCK)
        esp = f.read(esp_len)
        if len(esp) != esp_len:
            raise ToolError("could not read %s from the ISO" % esp_name)
        kind, errs, warns = fat_check(esp, esp_name)
        for w in warns:
            print("warning: " + w, file=sys.stderr)
        if errs:
            raise ToolError("; ".join(errs))

        f.seek(SECTOR)
        if f.read(SYSTEM_AREA - SECTOR).strip(b"\0") and not force:
            raise ToolError("ISO system area (sectors 1..63) is not empty; "
                            "refusing to overwrite it (use --force)")

        iso_sectors = size // SECTOR
        esp_first = align_up(iso_sectors, ALIGN_SECTORS)
        esp_secs = (esp_len + SECTOR - 1) // SECTOR
        esp_end = esp_first + esp_secs
        total = esp_end + GPT_ENTRY_SECTORS + 1
        last_lba = total - 1

        disk_guid = uuid.uuid5(GUID_NS, "disk:%s:%d:%s" % (
            hashlib.sha1(esp).hexdigest(), size, created.decode("latin-1")))
        entries = bytearray(GPT_ENTRIES * GPT_ENTRY_SIZE)
        entries[0:128] = gpt_entry(
            TYPE_BASIC, uuid.uuid5(disk_guid, "iso"),
            ISO_FIRST_LBA, iso_sectors - 1, NAME_ISO)
        entries[128:256] = gpt_entry(
            TYPE_ESP, uuid.uuid5(disk_guid, "esp"),
            esp_first, esp_end - 1, NAME_ESP)
        ecrc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF
        first_usable = 2 + GPT_ENTRY_SECTORS
        last_usable = last_lba - GPT_ENTRY_SECTORS - 1

        # appended ESP copy + backup GPT
        f.seek(size)
        f.write(bytes(esp_first * SECTOR - size))
        f.write(esp.ljust(esp_secs * SECTOR, b"\0"))
        f.write(bytes(entries))
        f.write(gpt_header(last_lba, 1, first_usable, last_usable, disk_guid,
                           last_lba - GPT_ENTRY_SECTORS, ecrc))
        # primary GPT in the ISO system area
        f.seek(SECTOR)
        f.write(gpt_header(1, last_lba, first_usable, last_usable, disk_guid,
                           2, ecrc))
        f.write(bytes(entries))
        # protective MBR (keep any boot code in the first 446 bytes)
        f.seek(0)
        mbr = bytearray(f.read(SECTOR).ljust(SECTOR, b"\0"))
        mbr[446:510] = bytes(64)
        mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0xEE,
                                   b"\xff\xff\xff", 1,
                                   min(total - 1, 0xFFFFFFFF))
        mbr[510:512] = b"\x55\xaa"
        f.seek(0)
        f.write(bytes(mbr))
        f.flush()
    print("%s: grafted GPT -- ISO %d MiB, ESP %s %d MiB at sector %d"
          % (path, size >> 20, kind, esp_len >> 20, esp_first))


# ── verify ─────────────────────────────────────────────────────────────────

def verify(path, esp_name):
    errs, warns, info = [], [], []
    with open(path, "rb") as f:
        size = f.seek(0, 2)
        total = size // SECTOR
        if size % SECTOR:
            errs.append("file size is not a multiple of 512")
        f.seek(0)
        mbr = f.read(SECTOR)
        if mbr[510:512] != b"\x55\xaa":
            errs.append("MBR: missing 0x55AA signature")
        types = [mbr[446 + 16 * i + 4] for i in range(4)]
        if 0xEE not in types:
            errs.append("MBR: no protective (0xEE) partition")

        f.seek(SECTOR)
        h1 = parse_gpt_header(f.read(SECTOR))
        f.seek((total - 1) * SECTOR)
        h2 = parse_gpt_header(f.read(SECTOR))
        if not h1:
            errs.append("primary GPT header missing or CRC bad")
        if not h2:
            errs.append("backup GPT header missing or CRC bad")
        parts = []
        if h1:
            parts, crc_ok = read_partitions(f, h1)
            if not crc_ok:
                errs.append("primary GPT entry array CRC mismatch")
            if h1["cur"] != 1 or h1["backup"] != total - 1:
                errs.append("primary GPT header LBAs are inconsistent")
            if h2 and (h2["guid"] != h1["guid"] or h2["cur"] != total - 1):
                errs.append("primary and backup GPT disagree")
            if h2:
                p2, ok2 = read_partitions(f, h2)
                if not ok2 or [(p["first"], p["last"]) for p in p2] != \
                        [(p["first"], p["last"]) for p in parts]:
                    errs.append("backup GPT entries differ from primary")
            last = -1
            for p in sorted(parts, key=lambda p: p["first"]):
                if p["first"] > p["last"] or p["first"] < h1["first"] \
                        or p["last"] > h1["last"]:
                    errs.append("partition %d lies outside the usable area"
                                % p["index"])
                if p["first"] <= last:
                    errs.append("partition %d overlaps another" % p["index"])
                last = max(last, p["last"])
        esp_parts = [p for p in parts if p["type"] == TYPE_ESP]
        if not esp_parts:
            errs.append("no EFI System Partition in the GPT")

        esp_in_iso = None
        try:
            _, root_lba, root_len, _ = read_pvd(f)
            esp_in_iso = iso_find(f, root_lba, root_len, esp_name)
        except ToolError as e:
            errs.append(str(e))
        if esp_parts:
            p = esp_parts[0]
            f.seek(p["first"] * SECTOR)
            blob = f.read((p["last"] - p["first"] + 1) * SECTOR)
            kind, e2, w2 = fat_check(blob, "ESP partition")
            errs += e2
            warns += w2
            info.append("ESP partition: %s, %d MiB, sectors %d-%d"
                        % (kind, len(blob) >> 20, p["first"], p["last"]))
            if esp_in_iso:
                f.seek(esp_in_iso[0] * ISO_BLOCK)
                ref = f.read(esp_in_iso[1])
                if blob[:len(ref)] != ref:
                    errs.append("ESP partition differs from %s inside the ISO"
                                % esp_name)
            else:
                errs.append("%s not found inside the ISO" % esp_name)
        try:
            tor = read_eltorito(f)
        except ToolError:
            tor = []
        if not tor:
            warns.append("no El Torito boot entry (optical UEFI boot will not work)")
        elif not any(pl == 0xEF for pl, _ in tor):
            errs.append("El Torito catalog has no EFI (0xEF) entry")
        elif esp_in_iso and not any(rba == esp_in_iso[0] for _, rba in tor):
            warns.append("El Torito EFI entry does not point at %s" % esp_name)
    for line in info:
        print(line)
    for w in warns:
        print("warning: " + w, file=sys.stderr)
    for e in errs:
        print("error: " + e, file=sys.stderr)
    if not errs:
        print("%s: UEFI/USB layout OK" % path)
    return not errs


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("image")
    ap.add_argument("--esp-name", default="esp.img")
    ap.add_argument("--verify", action="store_true", help="verify only")
    ap.add_argument("--force", action="store_true",
                    help="overwrite a non-empty ISO system area")
    args = ap.parse_args()
    try:
        if not args.verify:
            graft(args.image, args.esp_name, args.force)
        ok = verify(args.image, args.esp_name)
    except (ToolError, OSError) as e:
        print("iso_hybrid_gpt: " + str(e), file=sys.stderr)
        return 1
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())