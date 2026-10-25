#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent structural check of a finished bridge image.

    check_image.py --image bridge.bin --official original.bin [--pack rules.bbpack]

Re-parses the image from scratch: H3C header word sum, FIT hashes over the
UBI data, UBI headers and volume table, the SquashFS root and, with --pack,
that the pack sits right after the SquashFS at a LEB boundary.
"""
import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vendor_fit  # noqa: E402
import vendor_ubi  # noqa: E402


def fail(msg):
    sys.exit("FAIL: " + msg)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--image", required=True)
    ap.add_argument("--official", required=True)
    ap.add_argument("--pack")
    a = ap.parse_args()
    img = open(a.image, "rb").read()
    off = open(a.official, "rb").read()

    if sum(v[0] for v in struct.iter_unpack(">I", img[64:])) & 0xffffffff != 0xffffffff:
        fail("H3C word sum")
    if img[:68] != off[:68] or img[76:80] != off[76:80] or img[84:160] != off[84:160]:
        fail("H3C header fields other than sizes/checksum differ from the official image")
    fit_len = struct.unpack_from(">I", img, 164)[0]
    if 160 + fit_len > len(img) or len(img) - 160 - fit_len > 3:
        fail("FIT length")
    nodes = vendor_fit.fdt_props(img[160:160 + fit_len])
    imgs = [n for n in nodes if n.startswith("/images/") and n.count("/") == 2]
    if len(imgs) != 1:
        fail("FIT must hold one image")
    ubi = nodes[imgs[0]]["data"]
    crc = nodes[imgs[0] + "/hash@1"]["value"]
    sha1 = nodes[imgs[0] + "/hash@2"]["value"]
    if crc != struct.pack(">I", zlib.crc32(ubi) & 0xffffffff) or sha1 != hashlib.sha1(ubi).digest():
        fail("FIT hash values")
    with tempfile.TemporaryDirectory() as t:
        p = Path(t) / "b.ubi"
        p.write_bytes(ubi)
        meta, streams, _, _ = vendor_ubi.parse(p)
        rootfs = streams["ubi_rootfs"]
        if rootfs[:4] != b"hsqs":
            fail("ubi_rootfs is not SquashFS")
        used = struct.unpack_from("<Q", rootfs, 40)[0]
        (Path(t) / "r.sqfs").write_bytes(rootfs[:used])
        listing = subprocess.run(["unsquashfs", "-l", str(Path(t) / "r.sqfs")], capture_output=True,
                                 text=True)
        if listing.returncode or "squashfs-root/usr/sbin/bbd" not in listing.stdout:
            fail("SquashFS listing lacks /usr/sbin/bbd")
        pack_at = -(-used // vendor_ubi.LEB) * vendor_ubi.LEB
        if a.pack:
            pack = open(a.pack, "rb").read()
            if rootfs[pack_at:pack_at + len(pack)] != pack:
                fail("pack not found right after the SquashFS")
        elif rootfs[pack_at:pack_at + 4] == b"BBPK":
            fail("base image unexpectedly carries a pack")
    print(f"OK {a.image}: {len(img)} bytes, volumes "
          f"{[v['name'] for v in meta['volume_table']]}, squashfs {used} bytes, pack at {pack_at}")


if __name__ == "__main__":
    main()
