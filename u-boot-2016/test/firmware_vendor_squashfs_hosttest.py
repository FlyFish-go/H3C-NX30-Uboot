#!/usr/bin/env python3
"""Run the production UBI validator on synthetic images whose rootfs uses a
standard or a vendor-reordered SquashFS superblock (as in Jike QSDK packages).

Self-contained: needs gcc only. JIKE_UBI=/path/to.ubi additionally checks a
real UBI extracted from such a package.
"""
import os
import struct
import tempfile
import zlib
from pathlib import Path

from firmware_rt3000_b_ubi_hosttest import make_test_lib, validate

PEB, VID, DATA = 0x20000, 2048, 4096
LEB = PEB - DATA
LAYOUT_ID = 0x7fffffff - 4096


def crc(data):
    return zlib.crc32(data) ^ 0xffffffff


def ec_header(seq):
    h = bytearray(64)
    struct.pack_into(">IB3xQIII", h, 0, 0x55424923, 1, 0, VID, DATA, seq)
    struct.pack_into(">I", h, 60, crc(h[:60]))
    return h


def vid_header(vol, lnum, vol_type, compat=0, data_size=0, used_ebs=0, data_crc=0, sqnum=1):
    h = bytearray(64)
    struct.pack_into(">IBBBBII4xIIII", h, 0, 0x55424921, 1, vol_type, 0, compat,
                     vol, lnum, data_size, used_ebs, 0, data_crc)
    struct.pack_into(">Q", h, 40, sqnum)
    struct.pack_into(">I", h, 60, crc(h[:60]))
    return h


def vtbl(volumes):
    t = bytearray()
    for i in range(128):
        r = bytearray(172)
        if i < len(volumes):
            name, reserved, vol_type, flags = volumes[i]
            struct.pack_into(">IIIBBH", r, 0, reserved, 1, 0, vol_type, 0, len(name))
            r[16:16 + len(name)] = name.encode()
            r[144] = flags
        struct.pack_into(">I", r, 168, crc(r[:168]))
        t += r
    return t


def peb(seq, vid=None, data=b""):
    p = bytearray(b"\xff" * PEB)
    p[:64] = ec_header(seq)
    if vid is not None:
        p[VID:VID + 64] = vid
        p[DATA:DATA + len(data)] = data
    return p


def squashfs_lnum0(vendor, total_lebs):
    sb = bytearray(LEB)
    sb[:4] = b"hsqs"
    if vendor:
        # Field order changed after the magic: s_major is not at +28 and
        # +40 holds an unrelated value (taken from a real Jike rootfs).
        sb[4:96] = bytes.fromhex(
            "00000400040000000100c0068810541f0000000012004b0404002b00"
            "1858ad006a52ad00a227ad002c03ad000258ad002058ad00ffffffff"
            "ffffffffc417ff12a0f13804e7526e2e939cfc170c801c0009009000"
            "400000000400fd37")
    else:
        struct.pack_into("<H", sb, 28, 4)
        struct.pack_into("<Q", sb, 40, total_lebs * LEB - 100)
    return sb


def build(vendor=False, rootfs_lebs=(0, 1, 2), squash_lebs=3, magic=True):
    seq = 0x1234
    table = vtbl([("kernel", 4, 2, 0), ("ubi_rootfs", 8, 1, 0), ("rootfs_data", 1, 1, 1)])
    image = bytearray()
    for lnum in (0, 1):
        image += peb(seq, vid_header(LAYOUT_ID, lnum, 1, compat=5), table)
    kernel = bytearray(os.urandom(LEB + 100))
    struct.pack_into(">II", kernel, 0, 0xd00dfeed, len(kernel))
    for lnum, chunk in enumerate((kernel[:LEB], kernel[LEB:])):
        image += peb(seq, vid_header(0, lnum, 2, data_size=len(chunk), used_ebs=2,
                                     data_crc=crc(chunk), sqnum=10 + lnum), chunk)
    for lnum in rootfs_lebs:
        data = squashfs_lnum0(vendor, squash_lebs) if lnum == 0 else os.urandom(LEB)
        if lnum == 0 and not magic:
            data[:4] = b"sqsh"
        image += peb(seq, vid_header(1, lnum, 1, sqnum=20 + lnum), data)
    for _ in range(4):
        image += peb(seq)
    return image


def main():
    with tempfile.TemporaryDirectory(prefix="vendor-squashfs-hosttest-") as td:
        lib = make_test_lib(td)
        cases = [
            ("standard superblock, all LEBs", build(), True),
            ("standard superblock, last LEB missing", build(rootfs_lebs=(0, 1)), False),
            ("vendor superblock, contiguous LEBs", build(vendor=True), True),
            ("vendor superblock, single LEB", build(vendor=True, rootfs_lebs=(0,)), True),
            ("vendor superblock, hole at LEB 1", build(vendor=True, rootfs_lebs=(0, 2)), False),
            ("vendor superblock, LEB 0 missing", build(vendor=True, rootfs_lebs=(1, 2)), False),
            ("no SquashFS magic", build(vendor=True, magic=False), False),
        ]
        for name, image, want in cases:
            ret = validate(lib, image)[0]
            assert (ret == 0) == want, (name, ret)
            print(f"  PASS  {name} ({'accepted' if want else 'rejected'})")
        real = os.environ.get("JIKE_UBI")
        if real:
            ret, pebs, reserved = validate(lib, bytearray(Path(real).read_bytes()))
            assert ret == 0, ("JIKE_UBI", ret)
            print(f"  PASS  real vendor UBI accepted ({pebs} PEBs, {reserved} reserved)")
    print("VENDOR_SQUASHFS_HOSTTEST_PASS")


if __name__ == "__main__":
    main()
