#!/usr/bin/env python3
"""Run the production rescue UBI validator against upstream OpenWrt factory.ubi images.

usage: OPENWRT_UBI=a.ubi[:b.ubi...] python3 test/firmware_openwrt_ubi_hosttest.py
Each image must be accepted; truncated, kernel-less and CRC-damaged copies must be refused.
"""
import os
import struct
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from firmware_rt3000_b_ubi_hosttest import make_test_lib, validate, crc  # noqa: E402

PEB = 0x20000
LAYOUT = 0x7fffffff - 4096


def layout_pebs(img):
    return [pos for pos in range(0, len(img), PEB)
            if struct.unpack_from(">I", img, pos + 2048)[0] == 0x55424921
            and struct.unpack_from(">I", img, pos + 2048 + 8)[0] == LAYOUT]


def volumes(img):
    table = layout_pebs(img)[0] + 4096
    out = {}
    for i in range(128):
        rec = img[table + i * 172:table + (i + 1) * 172]
        n = struct.unpack_from(">H", rec, 14)[0]
        if n:
            out[rec[16:16 + n].decode()] = i
    return out


def main():
    paths = [p for p in os.environ.get("OPENWRT_UBI", "").split(":") if p]
    assert paths, "set OPENWRT_UBI"
    with tempfile.TemporaryDirectory(prefix="owrt-ubi-hosttest-") as td:
        lib = make_test_lib(td)
        for path in paths:
            img = bytearray(Path(path).read_bytes())
            vols = volumes(img)
            assert "kernel" in vols and "rootfs" in vols and "ubi_rootfs" not in vols, (path, vols)
            ret, pebs, reserved = validate(lib, img)
            assert ret == 0, (path, ret)
            # truncated (last image PEB missing)
            bad = bytearray(img[:len(img) - PEB])
            assert validate(lib, bad)[0] != 0, (path, "truncated accepted")
            # kernel volume removed from both layout copies
            bad = bytearray(img)
            for pos in layout_pebs(bad):
                rec = pos + 4096 + vols["kernel"] * 172
                bad[rec:rec + 168] = b"\0" * 168
                struct.pack_into(">I", bad, rec + 168, crc(bad[rec:rec + 168]))
            assert validate(lib, bad)[0] != 0, (path, "no kernel accepted")
            # EC header CRC damage
            bad = bytearray(img); bad[5] ^= 1
            assert validate(lib, bad)[0] != 0, (path, "EC crc accepted")
            print("ok %s pebs=%u reserved=%u vols=%s" % (Path(path).name, pebs, reserved, sorted(vols)))
    print("OPENWRT_UBI_HOSTTEST_PASS: %d images" % len(paths))


if __name__ == "__main__":
    main()
