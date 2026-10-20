#!/usr/bin/env python3
"""Run the synthetic BRIDGE_RETURN_ONLY registry path under static ARM QEMU.

All NAND/ENV bytes are ordinary disposable fixture files. bwrap exposes no
host device tree, MTD nodes, network, or device access to the test process.
"""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))
from test_install import make_fixture, write_env_entries, write_return_registry_header

sys.path.insert(0, str(PROJECT / "tools"))
import build_return_source
import bridge_kernel


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", type=Path, required=True, help="static ARM musl compiler")
    parser.add_argument("--qemu", type=Path, default=Path("/usr/bin/qemu-arm-static"))
    parser.add_argument("--bwrap", type=Path, default=Path("/usr/bin/bwrap"))
    parser.add_argument("--catalog", type=Path, required=True, help="pinned shared H3C NAND catalog header")
    parser.add_argument("--output", type=Path, required=True, help="new private disposable output directory")
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("use a new output directory")
    for executable in (args.cc, args.qemu, args.bwrap, args.catalog):
        if not executable.is_file():
            raise FileNotFoundError(executable)
    args.output.mkdir(parents=True, mode=0o700)
    guest = args.output / "guest"
    guest.mkdir()
    (guest / "qemu-arm").touch()
    # bwrap's root is the read-only guest tree, so the temporary fixture mount
    # point must exist before the bind is applied.
    (guest / "fixture").mkdir()
    catalog_include = args.output / "catalog-include"
    catalog_include.mkdir()
    shutil.copyfile(args.catalog, catalog_include / "h3c_nand_catalog.h")
    staging = args.cc.resolve().parents[2]
    env = dict(os.environ, STAGING_DIR=str(staging))
    source_products = {"RC3000": ("BOS_PRODUCT_RC3000", 2), "RT3000": ("BOS_PRODUCT_RT3000", 3),
                       "RW3000": ("BOS_PRODUCT_RW3000", 4), "NX30": ("BOS_PRODUCT_NX30", 1)}
    outputs = []
    for name, (product_macro, product_number) in source_products.items():
        official_sha = next(sha for sha, product in build_return_source.PRODUCTS.items()
                            if product == product_number)
        include = args.output / ("include-" + name)
        include.mkdir()
        shutil.copyfile(catalog_include / "h3c_nand_catalog.h", include / "h3c_nand_catalog.h")
        write_return_registry_header(include, official_sha, product_macro, name)
        binary = guest / ("registry-" + name)
        cmd = [str(args.cc), "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static",
               "-DBRIDGE_INSTALL_TEST", "-DBRIDGE_RETURN_ONLY", "-DBRIDGE_RETURN_REGISTRY_TEST",
               "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
               '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
               "-I", str(include), "-I", str(PROJECT / "src"),
               str(PROJECT / "tests/install_harness.c"), str(PROJECT / "src/bridge_install.c"),
               str(PROJECT / "src/bridge_version.c"), str(PROJECT / "src/bridge_oem_slot.c"),
               str(PROJECT / "src/bridge_backup.c"), str(PROJECT / "src/bridge_root_binding.c"),
               str(PROJECT / "src/bridge_first_install.c"), str(PROJECT / "src/bridge_install_identity.c"), "-o", str(binary)]
        subprocess.run(cmd, check=True, env=env)
        for route, (_, compatible, _) in bridge_kernel.ROUTES.items():
            fixture = args.output / (name + "-" + route.replace("@", "-").replace(".", "_"))
            make_fixture(fixture)
            product_name = "RX3000" if name == "NX30" and route != "config@mp02.1" else name
            write_env_entries(fixture, [("productname", product_name), ("machid", "8040000"),
                                        ("bootcmd", "bootipq"),
                                        ("bootargs", "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),
                                        ("bootdelay", "3")])
            (fixture / "proc/device-tree/compatible").write_bytes(
                compatible.encode() + b"\0qcom,ipq5018\0")
            command = [str(args.bwrap), "--unshare-all", "--die-with-parent", "--new-session",
                       "--ro-bind", str(guest), "/", "--ro-bind", str(args.qemu), "/qemu-arm",
                       "--bind", str(fixture), "/fixture", "--chdir", "/", "/qemu-arm",
                       "/" + binary.name, "/fixture", "status"]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if result.returncode:
                raise AssertionError(f"{name}/{route}: ARM harness rc={result.returncode}: {result.stderr[-1000:]}")
            state = json.loads(next(line for line in result.stdout.splitlines() if line.startswith("{")))
            if state["install_ready"] or not state["actions"]["return_oem"]:
                raise AssertionError(f"{name}/{route}: independent return readiness mismatch")
            if state["oem_return"]["source_sha256"] != official_sha:
                raise AssertionError(f"{name}/{route}: source registry identity mismatch")
            outputs.append({"source": name, "route": route, "return_ready": True,
                            "install_ready": False, "official_sha256": official_sha})
            shutil.rmtree(fixture)
        if name == "NX30":
            route = "config@mp02.1"
            _, compatible, _ = bridge_kernel.ROUTES[route]
            fixture = args.output / "nx30-asymmetric-bc"
            make_fixture(fixture)
            write_env_entries(fixture, [("productname", "NX30"), ("machid", "8040000"),
                                        ("bootcmd", "bootipq"),
                                        ("bootargs", "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),
                                        ("bootdelay", "3")])
            (fixture / "proc/device-tree/compatible").write_bytes(
                compatible.encode() + b"\0qcom,ipq5018\0")
            for index, marker in ((2, 0x31), (3, 0x72)):
                path = fixture / f"dev/mtd{index}"
                raw = bytearray(path.read_bytes()); raw[0x300] = marker; path.write_bytes(raw)
            command = [str(args.bwrap), "--unshare-all", "--die-with-parent", "--new-session",
                       "--ro-bind", str(guest), "/", "--ro-bind", str(args.qemu), "/qemu-arm",
                       "--bind", str(fixture), "/fixture", "--chdir", "/", "/qemu-arm",
                       "/" + binary.name, "/fixture", "return_oem"]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if result.returncode:
                raise AssertionError(f"asymmetric BC return failed: {result.stderr[-1000:]}")
            states = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
            state = states[-1]
            if state["phase"] != "complete":
                raise AssertionError("asymmetric BC return did not complete")
            for index, marker in ((2, 0x31), (3, 0x72)):
                raw = (fixture / f"dev/mtd{index}").read_bytes()
                if raw[0x300] != marker or int.from_bytes(raw[0x58:0x5c], "little") != 1:
                    raise AssertionError("BC non-target bytes or APP selector changed")
            shutil.rmtree(fixture)
            fixture = args.output / "nx30-partial-bc"
            make_fixture(fixture, fault="bc1_partial")
            write_env_entries(fixture, [("productname", "NX30"), ("machid", "8040000"),
                                        ("bootcmd", "bootipq"),
                                        ("bootargs", "console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"),
                                        ("bootdelay", "3")])
            (fixture / "proc/device-tree/compatible").write_bytes(
                compatible.encode() + b"\0qcom,ipq5018\0")
            command = [str(args.bwrap), "--unshare-all", "--die-with-parent", "--new-session",
                       "--ro-bind", str(guest), "/", "--ro-bind", str(args.qemu), "/qemu-arm",
                       "--bind", str(fixture), "/fixture", "--chdir", "/", "/qemu-arm",
                       "/" + binary.name, "/fixture", "return_oem"]
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if result.returncode:
                raise AssertionError(f"partial BC return failed: {result.stderr[-1000:]}")
            states = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
            state = states[-1]
            if state["phase"] != "failed" or not state["may_have_modified"] or state["actions"]["return_oem"]:
                raise AssertionError("partial BC write was not frozen as may-have-modified")
            shutil.rmtree(fixture)
    result = {"status": "PASS_ARM_SYNTHETIC_RETURN_REGISTRY", "device_operations": False,
              "qemu": str(args.qemu), "bwrap": str(args.bwrap), "results": outputs}
    (args.output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, separators=(",", ":")))


if __name__ == "__main__":
    main()
