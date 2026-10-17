#!/usr/bin/env python3
"""Inspect the exact diagnostic FIT shape against the production HW-A validator contract."""
import gzip
import hashlib
import pathlib
import struct
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: hw_a_recovery_fit_hosttest.py FIT")
fit = pathlib.Path(sys.argv[1]).resolve()
blob = fit.read_bytes()
assert len(blob) <= 0x01000000
assert len(blob) >= 8 and struct.unpack(">I", blob[4:8])[0] == len(blob)

def prop(path, name, kind="s"):
    return subprocess.check_output(["fdtget", "-t", kind, str(fit), path, name], text=True).strip()

assert prop("/configurations", "default") == "config@mp03.1"
conf = "/configurations/config@mp03.1"
assert prop(conf, "kernel") == "kernel@1"
assert prop(conf, "ramdisk") == "ramdisk@1"
assert prop(conf, "fdt") == "fdt@mp03.1"
assert prop(conf, "bootargs") == "console=ttyMSM0,115200,n8 rdinit=/init"
for node, image_type, arch, compression in [
    ("/images/kernel@1", "kernel", "arm", "lzma"),
    ("/images/ramdisk@1", "ramdisk", "arm", prop("/images/ramdisk@1", "compression")),
    ("/images/fdt@mp03.1", "flat_dt", "arm", "none"),
]:
    assert prop(node, "type") == image_type
    assert prop(node, "arch") == arch
    assert prop(node, "compression") == compression
    assert prop(node + "/hash@1", "algo") == "sha256"
assert int(prop("/images/kernel@1", "load", "x"), 16) == 0x41208000
assert int(prop("/images/kernel@1", "entry", "x"), 16) == 0x41208000
assert int(prop("/images/ramdisk@1", "load", "x"), 16) == 0x4c000000
assert int(prop("/images/ramdisk@1", "entry", "x"), 16) == 0x4c000000
assert "qcom,ipq5018-mp03.1" in subprocess.check_output(
    ["fdtget", "-t", "s", str(fit.parent / "rc3000-r012-mp03.1.dtb"), "/", "compatible"], text=True)
ramdisk_compression = prop("/images/ramdisk@1", "compression")
assert ramdisk_compression in ("none", "gzip")
ramdisk_path = fit.parent / ("initramfs.cpio" if ramdisk_compression == "none" else "initramfs.cpio.gz")
raw_ramdisk = ramdisk_path.read_bytes()
uncompressed_ramdisk = raw_ramdisk if ramdisk_compression == "none" else gzip.decompress(raw_ramdisk)
assert len(uncompressed_ramdisk) <= 16 * 1024 * 1024
assert 0x4c000000 + len(uncompressed_ramdisk) < 0x4e000000
assert 0x4e000000 + len(blob) <= 0x4f000000

print("HW-A diagnostic FIT topology/bounds: PASS")
print("FIT bytes:", len(blob))
print("FIT SHA256:", hashlib.sha256(blob).hexdigest())
