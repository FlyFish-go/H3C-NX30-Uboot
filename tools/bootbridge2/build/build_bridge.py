#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the BootBridge v2 base image for one official H3C firmware.

    build_bridge.py --official RT3000-original.bin --arm-bin out/arm --www web-device \
                    --ubinize ubinize --out DIR

Outputs in DIR:
  bridge-base.bin   OEM-upgrade image with an empty, reserved rule-pack area
  layout.json       where the pack goes and which checksums cover it (for the web
                    page and inject_pack.py)
  plan.json + plan.bin
                    COPY/XOR/LITERAL plan that rebuilds bridge-base.bin from the
                    official firmware (web/site engine format)

The bridge reuses the official kernel (with the reviewed DT transform of
bridge_kernel.py), wifi/bt volumes and a few OEM userspace files; everything
else in its rootfs is ours. Nothing proprietary is written outside DIR.
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import bridge_kernel  # noqa: E402
import compile_delta_plan  # noqa: E402
import vendor_fit  # noqa: E402
import vendor_ubi  # noqa: E402

PEB, LEB = vendor_ubi.PEB, vendor_ubi.LEB
EPOCH = 1791907200
PACK_LEBS = 32			# 3.9 MiB reserved for the rule pack
RUNTIME = HERE.parent / "runtime" / "boot"

# OEM userspace the bridge needs: the shell for boot.sh and the switch/NSS
# drivers for the LAN port. Everything is copied byte-identical.
OEM_FILES = ["bin/busybox", "sbin/kmodloader", "lib/firmware/qca-nss0-retail.bin",
             "lib/modules/4.4.60/qca-ssdk.ko", "lib/modules/4.4.60/qca-nss-dp.ko",
             "lib/modules/4.4.60/qca-nss-drv.ko"]
OEM_LIBS = ["libc.so", "ld-musl-arm.so.1", "libgcc_s.so.1", "libubox.so", "librpc.so"]
APPLETS = ["sh", "ash", "cat", "mkdir", "mknod", "mount", "ifconfig", "brctl", "ip", "sleep",
           "readlink", "grep", "awk", "route", "ls", "dmesg", "reboot"]
OUR_BINARIES = {"bbd": "usr/sbin/bbd", "bb_oem_return": "usr/sbin/bb-oem-return",
                "bb_facts": "usr/sbin/bb-facts"}


def sha(b, algo="sha256"):
    return hashlib.new(algo, b).hexdigest()


def run(args, **kw):
    r = subprocess.run([str(a) for a in args], capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit(f"command failed: {' '.join(map(str, args))}\n{r.stdout}{r.stderr}")
    return r.stdout


def split_official(official):
    """160-byte H3C header, then a FIT whose single ubi-* image is the system."""
    if len(official) < 168 or struct.unpack_from(">I", official, 160)[0] != 0xd00dfeed:
        sys.exit("not an H3C upgrade image (no FIT at 160)")
    fit_len = struct.unpack_from(">I", official, 164)[0]
    nodes = vendor_fit.fdt_props(official[160:160 + fit_len])
    ubi = [p["data"] for n, p in nodes.items() if n.startswith("/images/ubi-") and "data" in p]
    if len(ubi) != 1:
        sys.exit("official FIT does not hold exactly one UBI image")
    return ubi[0]


def stage_rootfs(oem_root, root, arm_bin, www):
    for d in ["dev", "proc", "sys", "tmp", "run", "etc/bridge", "usr/sbin", "usr/bin", "www",
              "root", "lib/modules/4.4.60", "lib/firmware", "sbin", "bin"]:
        (root / d).mkdir(parents=True, exist_ok=True)
    for rel in OEM_FILES + ["lib/" + n for n in OEM_LIBS]:
        src = oem_root / rel
        if src.is_symlink():
            target = os.readlink(src)
            if os.path.isabs(target) or ".." in Path(target).parts:
                sys.exit(f"unexpected OEM symlink {rel} -> {target}")
            (root / rel).symlink_to(target)
            continue
        if not src.is_file():
            sys.exit(f"official rootfs lacks {rel}")
        shutil.copyfile(src, root / rel)
        (root / rel).chmod(stat.S_IMODE(src.stat().st_mode))
    for name in OEM_LIBS:	# a library link must resolve inside the image
        p = root / "lib" / name
        if p.is_symlink() and not (root / "lib" / os.readlink(p)).exists():
            shutil.copyfile(oem_root / "lib" / os.readlink(p), root / "lib" / os.readlink(p))
    (root / "var").symlink_to("/tmp")
    for app in APPLETS:
        (root / "bin" / app).symlink_to("busybox")
    (root / "usr/sbin/insmod").symlink_to("../../sbin/kmodloader")
    (root / "lib/firmware/qca-nss0.bin").symlink_to("qca-nss0-retail.bin")
    for src, dst, mode in [("boot.sh", "etc/bridge/boot.sh", 0o755), ("profile", "etc/bridge/profile", 0o644),
                           ("rootfs-id", "etc/bridge/rootfs-id", 0o644), ("preinit", "etc/preinit", 0o755),
                           ("init", "init", 0o755)]:
        shutil.copyfile(RUNTIME / src, root / dst)
        (root / dst).chmod(mode)
    for name, dst in list(OUR_BINARIES.items()) + [("bb_init", "sbin/init"), ("bb_dhcp", "usr/sbin/bridge-dhcp")]:
        shutil.copyfile(arm_bin / name, root / dst)
        (root / dst).chmod(0o755)
    for f in ("index.html", "main.js", "style.css"):
        shutil.copyfile(www / f, root / "www" / f)
        (root / "www" / f).chmod(0o644)


def ubinize(ubinize_bin, table, images, work):
    lines = []
    for vol in table:
        lines += [f"[{vol['name']}]", "mode=ubi", f"vol_id={vol['id']}", f"vol_name={vol['name']}",
                  f"vol_type={vol['type']}", f"vol_size={vol['capacity']}"]
        if vol["name"] in images:
            if images[vol["name"]].stat().st_size > vol["capacity"]:
                sys.exit(f"{vol['name']} image exceeds the OEM volume capacity")
            lines.append(f"image={images[vol['name']]}")
        if vol["autoresize"]:
            lines.append("vol_flags=autoresize")
        lines.append("")
    cfg = work / "ubinize.cfg"
    cfg.write_text("\n".join(lines))
    out = work / "bridge.ubi"
    run([ubinize_bin, "-o", out, "-m", 2048, "-p", PEB, "-s", 2048, "-O", 2048, "-Q", EPOCH, cfg])
    return out


def pack_slots(ubi, vol_id, first_lnum):
    """File offsets (inside the UBI image) of the reserved LEBs, in LEB order."""
    slots = {}
    for i in range(len(ubi) // PEB):
        p = ubi[i * PEB:(i + 1) * PEB]
        if p[:4] != b"UBI#":
            continue
        vid, data = struct.unpack_from(">II", p, 16)
        v = p[vid:vid + 64]
        if v[:4] != b"UBI!":
            continue
        vol, lnum = struct.unpack_from(">II", v, 8)
        if vol == vol_id and first_lnum <= lnum < first_lnum + PACK_LEBS:
            if v[5] != 1:
                sys.exit("ubi_rootfs must be a dynamic volume (no data CRC)")
            slots[lnum] = i * PEB + data
    if sorted(slots) != list(range(first_lnum, first_lnum + PACK_LEBS)):
        sys.exit("reserved pack LEBs missing from the UBI image")
    return [slots[n] for n in sorted(slots)]


def build_fit(ubi, work):
    node = "ubi-bridge-" + sha(ubi, "sha1")
    crc = struct.pack(">I", zlib.crc32(ubi) & 0xffffffff)
    sha1 = hashlib.sha1(ubi).digest()
    its = ('/dts-v1/; / { description = "BootBridge v2"; #address-cells = <1>; '
           'timestamp = <%d>; images { %s { description = "BootBridge UBI"; '
           'data = /incbin/("bridge.ubi"); type = "firmware"; arch = "arm"; compression = "none"; '
           'hash@1 { algo = "crc32"; value = [%s]; }; hash@2 { algo = "sha1"; value = [%s]; }; }; }; '
           'configurations { default = "conf-bridge"; conf-bridge { firmware = "%s"; }; }; };') % (
        EPOCH, node, crc.hex(" "), sha1.hex(" "), node)
    (work / "bridge.its").write_text(its)
    run(["dtc", "-I", "dts", "-O", "dtb", "-o", work / "bridge.itb", work / "bridge.its"], cwd=work)
    fit = (work / "bridge.itb").read_bytes()
    nodes = vendor_fit.fdt_props(fit)
    img = nodes["/images/" + node]
    if img["data"] != ubi:
        sys.exit("FIT data differs from the UBI image")

    def unique(needle, what):
        at = fit.find(needle)
        if at < 0 or fit.find(needle, at + 1) >= 0:
            sys.exit(f"cannot locate {what} uniquely in the FIT")
        return at
    return fit, {"data_offset": unique(ubi, "UBI data"), "data_len": len(ubi),
                 "crc32_offset": unique(crc, "crc32 value"), "sha1_offset": unique(sha1, "sha1 value")}


def wrap_h3c(official, fit):
    payload = fit + b"\0" * (-len(fit) % 4)
    out = bytearray(official[:160]) + payload
    struct.pack_into(">I", out, 68, 96 + len(payload))
    struct.pack_into(">I", out, 80, len(payload))
    struct.pack_into(">I", out, 72, 0)
    total = sum(v[0] for v in struct.iter_unpack(">I", bytes(out[64:]))) & 0xffffffff
    struct.pack_into(">I", out, 72, (0xffffffff - total) & 0xffffffff)
    if sum(v[0] for v in struct.iter_unpack(">I", bytes(out[64:]))) & 0xffffffff != 0xffffffff:
        sys.exit("H3C header checksum failed")
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--official", required=True, type=Path)
    ap.add_argument("--arm-bin", required=True, type=Path)
    ap.add_argument("--www", required=True, type=Path)
    ap.add_argument("--ubinize", required=True)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()

    official = a.official.read_bytes()
    official_sha = sha(official)
    if official_sha not in bridge_kernel.SOURCES:
        sys.exit("unsupported official firmware (not one of the reviewed four)")
    product = bridge_kernel.SOURCES[official_sha][0]
    a.out.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory() as t:
        work = Path(t)
        (work / "stock.ubi").write_bytes(split_official(official))
        meta, streams, _, _ = vendor_ubi.parse(work / "stock.ubi")
        table = meta["volume_table"]
        rootfs_vol = next(v for v in table if v["name"] == "ubi_rootfs")
        if rootfs_vol["type"] != "dynamic":
            sys.exit("OEM ubi_rootfs is not dynamic")

        (work / "oem.squashfs").write_bytes(streams["ubi_rootfs"])
        run(["unsquashfs", "-no-xattrs", "-d", work / "oem", work / "oem.squashfs",
             *OEM_FILES, *["lib/" + n for n in OEM_LIBS]])
        root = work / "root"
        stage_rootfs(work / "oem", root, a.arm_bin, a.www)
        (work / "nodes.pseudo").write_text("dev/console c 0600 0 0 5 1\n")
        run(["mksquashfs", root, work / "bridge.squashfs", "-comp", "xz", "-b", "131072", "-noappend",
             "-all-root", "-no-xattrs", "-no-progress", "-processors", "1", "-mkfs-time", EPOCH,
             "-all-time", EPOCH, "-pf", work / "nodes.pseudo"])
        squash = (work / "bridge.squashfs").read_bytes()
        # The runtime finds the pack from the SquashFS bytes_used field, so the
        # boundary must be derived from it, not from the padded file length.
        used = struct.unpack_from("<Q", squash, 40)[0]
        first_lnum = -(-used // LEB)
        if len(squash) > first_lnum * LEB:
            first_lnum = -(-len(squash) // LEB)
            if -(-used // LEB) != first_lnum:
                sys.exit("SquashFS padding crosses a LEB boundary")
        rootfs_img = squash + b"\xff" * (first_lnum * LEB - len(squash))
        # Reserved area stays 0xff: it costs nothing in the web page's plan
        # data, and the runtime only accepts it once it starts with "BBPK".
        (work / "rootfs.img").write_bytes(rootfs_img + b"\xff" * (PACK_LEBS * LEB))

        kernel, kernel_report = bridge_kernel.transform_kernel(streams["kernel"], official_sha)
        (work / "kernel.fit").write_bytes(kernel)
        (work / "wifi_fw.bin").write_bytes(streams["wifi_fw"])
        (work / "bt_fw.bin").write_bytes(streams["bt_fw"])
        images = {"kernel": work / "kernel.fit", "wifi_fw": work / "wifi_fw.bin",
                  "bt_fw": work / "bt_fw.bin", "ubi_rootfs": work / "rootfs.img"}
        ubi_path = ubinize(a.ubinize, table, images, work)
        ubi = ubi_path.read_bytes()

        new_meta, new_streams, _, _ = vendor_ubi.parse(ubi_path)
        if new_meta["volume_table"] != table:
            sys.exit("bridge UBI volume table differs from the OEM one")
        for name in ("wifi_fw", "bt_fw"):
            if new_streams[name] != streams[name]:
                sys.exit(f"{name} changed")
        if new_streams["kernel"] != kernel or not new_streams["ubi_rootfs"].startswith(rootfs_img):
            sys.exit("kernel or rootfs volume content differs")

        slots = pack_slots(ubi, rootfs_vol["id"], first_lnum)
        fit, fit_loc = build_fit(ubi, work)
        base = wrap_h3c(official, fit)

    fit_off = 160
    layout = {
        "schema": "bb-layout-v1", "product": product,
        "official": {"bytes": len(official), "sha256": official_sha},
        "base": {"bytes": len(base), "sha256": sha(base)},
        "pack": {"slot_bytes": LEB, "slots": [fit_off + fit_loc["data_offset"] + s for s in slots],
                 "capacity": LEB * PACK_LEBS},
        "fit": {"data_offset": fit_off + fit_loc["data_offset"], "data_len": fit_loc["data_len"],
                "crc32_offset": fit_off + fit_loc["crc32_offset"],
                "sha1_offset": fit_off + fit_loc["sha1_offset"]},
        "header": {"checksum_offset": 72, "sum_from": 64},
        "rootfs": {"squashfs_bytes": len(squash), "pack_lnum": first_lnum},
        "kernel": {"stock_sha256": kernel_report["stock_fit_sha256"],
                   "bridge_sha256": kernel_report["output_fit_sha256"]},
    }
    (a.out / "bridge-base.bin").write_bytes(base)
    (a.out / "layout.json").write_text(json.dumps(layout, indent=2) + "\n")

    ops, data, _ = compile_delta_plan.build_plan(official, base)
    if compile_delta_plan.reconstruct(official, ops, data, len(base)) != base:
        sys.exit("delta plan does not rebuild the base image")
    kinds = {"COPY": 1, "LITERAL": 2, "XOR": 3}
    plan = {"id": product, "inSize": len(official), "inSha": official_sha, "outSize": len(base),
            "outSha": sha(base), "ops": [[kinds[o.kind], o.offset, o.size] for o in ops],
            "data": {"size": len(data), "sha": sha(data)}}
    (a.out / "plan.json").write_text(json.dumps(plan) + "\n")
    (a.out / "plan.bin").write_bytes(data)
    print(f"{product}: base {len(base)} bytes, squashfs {len(squash)}, pack area {LEB * PACK_LEBS} "
          f"bytes in {PACK_LEBS} LEBs, plan {len(ops)} ops / {len(data)} literal bytes")


if __name__ == "__main__":
    main()
