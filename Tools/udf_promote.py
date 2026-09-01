#!/usr/bin/env python3
"""
Promote a genisoimage "-udf" bridge image so it is recognised as UDF.

genisoimage always lays the ISO9660 volume-descriptor set (Primary VD, the
El Torito Boot Record VD, and a VD Set Terminator) into logical sectors
16.. and only then writes the UDF Volume Recognition Sequence
(BEA01 / NSR02 / TEA01). That pushes NSR02 past sector 19, where `file`
(and some OS probes) look for it on an ISO/UDF bridge, so the disc keeps
reporting as plain "ISO 9660".

The ISO9660 VD Set Terminator carries no data -- every ISO9660 reader
stops at the first non-"CD001" descriptor anyway (our BootManager reads
only the Primary VD at sector 16). So: drop that terminator sector and
slide the UDF VRS up by one, leaving

    16  ISO9660 Primary VD          (BootManager, ISO probes)
    17  El Torito Boot Record VD    (UEFI firmware)
    18  BEA01                       }
    19  NSR02                       }  UDF Volume Recognition Sequence
    20  TEA01                       }

after which the image is "ISO 9660 ... + UDF filesystem data".

Idempotent and conservative: if the exact terminator+BEA01 pattern is not
found the file is left untouched.
"""
import sys

SECTOR = 2048


def main(path: str) -> int:
    with open(path, "r+b") as f:
        def rd(s):
            f.seek(s * SECTOR)
            return f.read(SECTOR)

        def wr(s, b):
            f.seek(s * SECTOR)
            f.write(b)

        # scan the recognition area for  <CD001 type-255>  <BEA01>
        for term in range(17, 22):
            here = rd(term)
            after = rd(term + 1)
            if here[0] == 0xFF and here[1:6] == b"CD001" and after[1:6] == b"BEA01":
                tail = [rd(term + 1 + k) for k in range(8)]
                for k, blk in enumerate(tail):
                    wr(term + k, blk)
                wr(term + len(tail), b"\x00" * SECTOR)
                print(f"udf_promote: dropped ISO9660 VD terminator at sector {term}; "
                      f"UDF VRS now at {term}..{term + 2}")
                return 0

        print("udf_promote: no ISO9660-terminator / BEA01 pattern found -- left as is")
        return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: udf_promote.py <image.iso>")
    sys.exit(main(sys.argv[1]))
