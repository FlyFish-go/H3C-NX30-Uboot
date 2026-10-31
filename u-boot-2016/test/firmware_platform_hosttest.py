#!/usr/bin/env python3
"""Run the real rt3_fw_ubi_platform() on UBI images whose kernel volume holds
a FIT built with dtc. JIKE_UBI=a.ubi[:b.ubi] also prints real images."""
import os
import random
import subprocess
import tempfile
from pathlib import Path

from firmware_vendor_squashfs_hosttest import LAYOUT_ID, LEB, crc, peb, vid_header, vtbl

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <rt3_fw_platform.h>
int main(int argc, char **argv)
{
 FILE *f = fopen(argv[1], "rb"); long n; void *buf; struct rt3_fw_platform p; int rc;
 fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
 buf = malloc(n); if (fread(buf, 1, n, f) != (size_t)n) return 2;
 rc = rt3_fw_ubi_platform(buf, n, (const char *const *)argv + 2, argc - 2, &p);
 if (rc) printf("%d\n", rc);
 else printf("0|%s|%u|%s|%s\n", p.configs, p.count, p.arch, p.match);
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
    exe = tmp / "platform"
    subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-DRT3_FW_HOST_TEST",
                    "-DUSE_HOSTCC", "-I" + str(ROOT / "tools"), "-idirafter", str(ROOT / "include"),
                    str(tmp / "main.c"), str(ROOT / "failsafe/modules/firmware_platform.c"),
                    *objs, "-o", str(exe)], check=True)
    return exe


def fit(tmp, configs, default=None, arch="arm"):
    blob = tmp / "kernel.bin"
    blob.write_bytes(os.urandom(LEB * 2 + 5000))     # FIT spans three LEBs
    conf = "".join(f'{name} {{ kernel = "kernel@1"; fdt = "fdt@1"; }};' for name in configs or [])
    dts = ('/dts-v1/; / { images { kernel@1 { data = /incbin/("%s"); arch = "%s"; }; '
           'fdt@1 { data = [00]; }; }; %s };' %
           (blob, arch, ('configurations { %s %s };' % (f'default = "{default}";' if default else "", conf))
            if configs is not None else ""))
    (tmp / "k.dts").write_text(dts)
    subprocess.run(["dtc", "-q", "-I", "dts", "-O", "dtb", "-o", str(tmp / "k.itb"), str(tmp / "k.dts")],
                   check=True)
    return (tmp / "k.itb").read_bytes()


def ubi(kernel, drop_lnum=None):
    seq = 0x5678
    table = vtbl([("kernel", 40, 2, 0), ("ubi_rootfs", 8, 1, 0)])
    pebs = [peb(seq, vid_header(LAYOUT_ID, lnum, 1, compat=5), table) for lnum in (0, 1)]
    chunks = [kernel[i:i + LEB] for i in range(0, len(kernel), LEB)]
    for lnum, chunk in enumerate(chunks):
        if lnum != drop_lnum:
            pebs.append(peb(seq, vid_header(0, lnum, 2, data_size=len(chunk), used_ebs=len(chunks),
                                            data_crc=crc(chunk), sqnum=10 + lnum), chunk))
    pebs.append(peb(seq, vid_header(1, 0, 1, sqnum=99), b"hsqs"))
    random.Random(1).shuffle(pebs)                    # LEB order must come from lnum
    return b"".join(pebs)


def run(exe, tmp, image, *names):
    (tmp / "u.ubi").write_bytes(image)
    return subprocess.run([str(exe), str(tmp / "u.ubi"), *names], check=True,
                          capture_output=True, text=True).stdout.strip()


def main():
    with tempfile.TemporaryDirectory(prefix="rt3-platform-") as td:
        tmp = Path(td)
        exe = build(tmp)
        many = [f"config@db-mp0{i}.{j}" for i in range(1, 5) for j in range(1, 4)] + ["config@mp03.1"]
        cases = [
            ("default config, two configs; board order decides",
             ubi(fit(tmp, ["config@mp02.1", "config@mp03.1"], "config@mp02.1")),
             ("config@mp03", "config@mp03.1", "config@mp02.1"), "0|config@mp02.1,config@mp03.1|2|arm|config@mp03.1"),
            ("arm64 kernel, no matching config",
             ubi(fit(tmp, ["config@mp03.5-c1"], "config@mp03.5-c1", "arm64")),
             ("config@mp02.1", "config-mp02.1"), "0|config@mp03.5-c1|1|arm64|"),
            ("no default: first config's kernel", ubi(fit(tmp, ["config@mp02.1"])), (),
             "0|config@mp02.1|1|arm|"),
            ("unsafe characters are replaced", ubi(fit(tmp, ["config@mp02.1"], None, 'ar\\"m<')), (),
             "0|config@mp02.1|1|ar?m?|"),
            # Like OEM FITs: the list overflows the display buffer, the match must not.
            ("13 configs, board's one listed last",
             ubi(fit(tmp, many, many[0])), ("config@mp03.1",),
             "0|" + ",".join(many[:9]) + "|13|arm|config@mp03.1"),
            ("no configurations node", ubi(fit(tmp, None)), (), "-2"),
            ("kernel LEB missing", ubi(fit(tmp, ["config@mp02.1"]), drop_lnum=1), (), "-22"),
        ]
        for name, image, names, want in cases:
            got = run(exe, tmp, image, *names)
            assert got == want, (name, got, want)
            print(f"  PASS  {name}: {got}")
        for real in filter(None, os.environ.get("JIKE_UBI", "").split(":")):
            got = run(exe, tmp, Path(real).read_bytes(), "config@mp02.1", "config@mp03.1", "config@mp03.1-c2")
            assert got.startswith("0|"), (real, got)
            print(f"  PASS  {Path(real).name}: {got}")
    print("PLATFORM_HOSTTEST_PASS")


if __name__ == "__main__":
    main()
