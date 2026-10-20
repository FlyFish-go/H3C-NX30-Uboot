#!/usr/bin/env python3
"""ARM/QEMU, bwrap-isolated tests for compiled OEM candidate patch reconstruction."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tests"))
from test_install import make_fixture, write_test_oem_manifest_header
from test_payload_patch_runtime import APPSBL, patch_header


def compile_binary(cc, output, include, catalog_include, flags, sources):
    command = [str(cc), "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static",
               *flags, "-I", str(include), "-I", str(catalog_include),
               "-I", str(PROJECT / "src"), *map(str, sources),
               "-o", str(output)]
    subprocess.run(command, check=True, capture_output=True, text=True, timeout=120)


def run_guest(args, guest, binary, fixture, mode, bind_input=None):
    command = [str(args.bwrap), "--unshare-all", "--die-with-parent", "--new-session",
               "--ro-bind", str(guest), "/", "--ro-bind", str(args.qemu), "/qemu-arm"]
    if bind_input:
        command += ["--ro-bind", str(bind_input), "/input.bin"]
    if fixture:
        command += ["--bind", str(fixture), "/fixture"]
    command += ["--chdir", "/", "/qemu-arm", "/" + binary.name]
    if fixture:
        command += ["/fixture", mode]
    else:
        command += ["/input.bin"]
    return subprocess.run(command, capture_output=True, timeout=90)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cc", required=True, type=Path)
    parser.add_argument("--qemu", required=True, type=Path)
    parser.add_argument("--bwrap", required=True, type=Path)
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    for item in (args.cc, args.qemu, args.bwrap, args.catalog):
        if not item.is_file():
            raise FileNotFoundError(item)
    args.output.mkdir(parents=True, mode=0o700)
    guest = args.output / "guest"
    guest.mkdir()
    (guest / "fixture").mkdir()
    (guest / "input.bin").touch()
    (guest / "qemu-arm").touch()
    catalog_dir = args.output / "catalog"
    catalog_dir.mkdir()
    shutil.copyfile(args.catalog, catalog_dir / "h3c_nand_catalog.h")

    base = bytes((i * 29 + 17) & 255 for i in range(APPSBL))
    target = bytearray(base)
    target[37:42] = b"PATCH"
    target[0x3000:0x3004] = b"OEM!"
    target = bytes(target)
    ranges = [(37, 5), (0x3000, 4)]
    base_path = args.output / "base.bin"
    base_path.write_bytes(base)
    bad_base = bytearray(base);bad_base[11] ^= 1
    bad_base_path = args.output / "bad-base.bin"
    bad_base_path.write_bytes(bad_base)

    common_sources = [PROJECT / "src/bridge_install.c", PROJECT / "src/bridge_version.c",
                      PROJECT / "src/bridge_oem_slot.c", PROJECT / "src/bridge_backup.c",
                      PROJECT / "src/bridge_root_binding.c", PROJECT / "src/bridge_first_install.c",
                      PROJECT / "src/bridge_install_identity.c"]
    flags = ["-DBRIDGE_INSTALL_TEST", "-DBRIDGE_PAYLOAD_PATCH_TEST",
             '-DBI_PAYLOAD_PATCH_HEADER="bridge_payload_patch.h"']
    outcomes = []
    for name, header, source_image, expected_rc, expected in (
        ("good", patch_header(base, target, ranges), base_path, 0, target),
        ("wrong-final", patch_header(base, target, ranges,
                                      b"WRONG" + target[0x3000:0x3004]), base_path, 5, None),
        ("bad-bounds", patch_header(base, target, [(APPSBL - 1, 2)]), base_path, 5, None),
        ("zero-patch", patch_header(base, base, []), base_path, 0, base),
        ("bad-zero-sentinel", patch_header(base, base, []).replace(
            "{0u,0u,0u}", "{1u,0u,0u}"), base_path, 5, None),
        ("zero-count-nonzero-data", patch_header(base, base, [], patch_data=b"\x00"),
         base_path, 5, None),
    ):
        include = args.output / (name + "-include");include.mkdir()
        (include / "bridge_payload_patch.h").write_text(header)
        binary = guest / ("patch-" + name)
        compile_binary(args.cc, binary, include, catalog_dir, flags,
                       [PROJECT / "tests/payload_patch_harness.c", *common_sources])
        result = run_guest(args, guest, binary, None, "", source_image)
        if result.returncode != expected_rc:
            raise AssertionError(f"{name}: ARM QEMU exit {result.returncode}, expected {expected_rc}; {result.stderr[-800:]}")
        if expected is not None and result.stdout != expected:
            raise AssertionError("good ARM reconstruction differs from exact target candidate")
        outcomes.append({"case": name, "passed": True})

    good_binary = guest / "patch-good"
    result = run_guest(args, guest, good_binary, None, "", bad_base_path)
    if result.returncode != 5:
        raise AssertionError(f"bad base: expected rejection code 5, got {result.returncode}")
    outcomes.append({"case": "bad-base", "passed": True})

    read_include = args.output / "read-payload-include";read_include.mkdir()
    (read_include / "bridge_payload_patch.h").write_text(patch_header(base, target, ranges))
    write_test_oem_manifest_header(read_include, hashlib.sha256(target).hexdigest())
    read_binary = guest / "read-payload-provider"
    compile_binary(args.cc, read_binary, read_include, catalog_dir,
                   ["-DBRIDGE_INSTALL_TEST", "-DBRIDGE_PAYLOAD_PATCH_TEST",
                    "-DBRIDGE_OEM_COMPILED_TEST_MANIFEST",
                    '-DBI_PAYLOAD_PATCH_HEADER="bridge_payload_patch.h"'],
                   [PROJECT / "tests/install_harness.c", *common_sources])
    fixture = args.output / "fixture-data"
    make_fixture(fixture)
    (fixture / "etc/bridge/uboot.bin").write_bytes(base)
    status = run_guest(args, guest, read_binary, fixture, "status")
    if status.returncode:
        raise AssertionError(f"read_payload ARM fixture failed: {status.stderr[-1000:]}")
    state = next(json.loads(line) for line in status.stdout.decode().splitlines() if line.startswith("{"))
    if not any(check["name"] == "candidate_payload" and check["status"] == "pass"
               for check in state["checks"]):
        raise AssertionError("read_payload did not publish exact reconstructed candidate as available")
    outcomes.append({"case": "read-payload-caller", "passed": True})

    result = {"status": "PASS_ARM_SYNTHETIC_PAYLOAD_PATCH", "device_operations": False,
              "network_access": False, "results": outcomes}
    (args.output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, separators=(",", ":")))


if __name__ == "__main__":
    main()
