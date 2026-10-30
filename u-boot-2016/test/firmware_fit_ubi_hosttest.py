#!/usr/bin/env python3
"""Run the real rt3_fw_fit_ubi() on QSDK-style FIT packages built with dtc."""
import errno
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <rt3_fw_fit_ubi.h>
int main(int argc, char **argv)
{
 (void)argc;
 FILE *f = fopen(argv[1], "rb"); long n; void *buf; const void *ubi = NULL; size_t len = 0; int rc;
 fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
 buf = malloc(n + 64); if (fread(buf, 1, n, f) != (size_t)n) return 2;
 rc = rt3_fw_fit_ubi(buf, n, &ubi, &len);
 printf("%d %ld %zu\n", rc, ubi ? (long)((const char *)ubi - (const char *)buf) : -1L, len);
 return 0;
}
'''


def build(tmp):
    objs = []
    for name in ("fdt", "fdt_ro"):
        obj = tmp / f"{name}.o"
        subprocess.run(["cc", "-c", "-std=gnu99", "-DUSE_HOSTCC", "-I" + str(ROOT / "tools"),
                        "-idirafter", str(ROOT / "include"), "-o", str(obj),
                        str(ROOT / f"lib/libfdt/{name}.c")], check=True)
        objs.append(str(obj))
    (tmp / "main.c").write_text(HARNESS)
    exe = tmp / "fit-ubi"
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DRT3_FW_HOST_TEST",
                    "-DUSE_HOSTCC", "-I" + str(ROOT / "tools"), "-idirafter", str(ROOT / "include"),
                    str(tmp / "main.c"), str(ROOT / "failsafe/modules/firmware_fit_ubi.c"),
                    *objs, "-o", str(exe)], check=True)
    return exe


def fit(tmp, name, images):
    nodes = []
    for i, (node, payload) in enumerate(images):
        blob = tmp / f"{name}-{i}.bin"
        blob.write_bytes(payload)
        nodes.append(f'{node} {{ description = "{node}"; type = "firmware"; data = /incbin/("{blob}"); }};')
    dts = tmp / f"{name}.dts"
    dts.write_text('/dts-v1/;\n/ { description = "Flashing nand 800 20000"; #address-cells = <1>;\n'
                   ' images { ' + " ".join(nodes) + ' };\n};\n')
    out = tmp / f"{name}.itb"
    subprocess.run(["dtc", "-I", "dts", "-O", "dtb", "-o", str(out), str(dts)], check=True,
                   stderr=subprocess.DEVNULL)
    return out.read_bytes()


def run(exe, tmp, data):
    path = tmp / "input.bin"
    path.write_bytes(data)
    rc, off, length = subprocess.run([str(exe), str(path)], check=True, capture_output=True,
                                     text=True).stdout.split()
    return int(rc), int(off), int(length)


def main():
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        exe = build(tmp)
        ubi = b"UBI#" + bytes(range(256)) * 512
        script = b"echo 'flash.scr'\nnand erase 0x900000 0x2800000\n"
        good = fit(tmp, "good", [("script", script), ("ubi-6396c55d", ubi)])
        footer = b"<GMFOOTER><finger>7d2154591cc6056ec4c88a02c82c9722</finger></GMFOOTER>"

        for label, data in (("bare FIT", good), ("vendor footer", good + footer)):
            rc, off, length = run(exe, tmp, data)
            assert rc == 0 and length == len(ubi) and data[off:off + length] == ubi, label
        assert run(exe, tmp, good + b"\0" * 4097)[0] == -errno.EINVAL
        assert run(exe, tmp, good[:-16])[0] == -errno.EINVAL
        assert run(exe, tmp, bytes(range(256)) * 64)[0] == -errno.EINVAL
        bad = bytearray(good)
        struct.pack_into(">I", bad, 12, len(good) + 4096)  # off_dt_strings past totalsize
        assert run(exe, tmp, bytes(bad))[0] == -errno.EINVAL
        none = fit(tmp, "none", [("script", script), ("kernel", b"\xd0\x0d\xfe\xed" + b"k" * 64)])
        assert run(exe, tmp, none)[0] == -errno.ENOENT
        two = fit(tmp, "two", [("script", script), ("ubi-a", ubi), ("ubi-b", ubi)])
        assert run(exe, tmp, two)[0] == -errno.EEXIST
    print("PASS: QSDK FIT UBI unwrap (footer, bounds, none, ambiguous, truncated, garbage)")


if __name__ == "__main__":
    main()
