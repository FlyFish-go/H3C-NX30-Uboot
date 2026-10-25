#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write a rule pack into a bridge base image (reference for the web page).

    inject_pack.py --base bridge-base.bin --layout layout.json --pack rules.bbpack -o bridge.bin

Only three things change: the reserved LEB data areas get the pack bytes, the
FIT image's crc32/sha1 values are recomputed over the UBI data, and the H3C
header checksum is fixed so the 32-bit big-endian word sum from offset 64 is
0xffffffff. The UBI headers stay valid because ubi_rootfs is a dynamic
volume (no data CRC).
"""
import argparse
import hashlib
import json
import struct
import sys
import zlib


def inject(base, layout, pack):
    out = bytearray(base)
    p = layout["pack"]
    if hashlib.sha256(base).hexdigest() != layout["base"]["sha256"]:
        raise ValueError("base image does not match layout")
    if len(pack) > p["capacity"] or pack[:4] != b"BBPK":
        raise ValueError("pack too large or not a pack")
    pos = 0
    for slot in p["slots"]:
        if pos >= len(pack):
            break
        n = min(p["slot_bytes"], len(pack) - pos)
        out[slot:slot + n] = pack[pos:pos + n]
        pos += n
    f = layout["fit"]
    data = bytes(out[f["data_offset"]:f["data_offset"] + f["data_len"]])
    out[f["crc32_offset"]:f["crc32_offset"] + 4] = struct.pack(">I", zlib.crc32(data) & 0xffffffff)
    out[f["sha1_offset"]:f["sha1_offset"] + 20] = hashlib.sha1(data).digest()
    h = layout["header"]
    struct.pack_into(">I", out, h["checksum_offset"], 0)
    total = sum(v[0] for v in struct.iter_unpack(">I", bytes(out[h["sum_from"]:]))) & 0xffffffff
    struct.pack_into(">I", out, h["checksum_offset"], (0xffffffff - total) & 0xffffffff)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", required=True)
    ap.add_argument("--layout", required=True)
    ap.add_argument("--pack", required=True)
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()
    out = inject(open(a.base, "rb").read(), json.load(open(a.layout)), open(a.pack, "rb").read())
    open(a.output, "wb").write(out)
    print(f"{a.output}: {len(out)} bytes sha256 {hashlib.sha256(out).hexdigest()}", file=sys.stderr)


if __name__ == "__main__":
    main()
