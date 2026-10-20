#!/usr/bin/env python3
"""Compile and exercise the production compiled first-install qualifier.

This test feeds exact public candidate and official boot-chain bytes through
the real builder header generators, then supplies synthetic read-only facts to
the production predicate. It is not a device or hardware qualification.
"""
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zlib

PROJECT = Path(__file__).resolve().parents[1]
REPO = PROJECT.parents[1]
sys.path.insert(0, str(PROJECT / "tools"))

import bridge_kernel
import build
import build_return_source as return_source
import vendor_fit
import vendor_ubi
import root_binding_contract

PUBLIC_ARCHIVE = REPO / "artifacts/ABCD-UBOOT-SOURCE-AND-CANDIDATES-20261016.tar.gz"
PUBLIC_CATALOG_MEMBER = (
    "abcd-uboot-public-review-20261016/source/u-boot-2016/include/h3c_nand_catalog.h"
)
OFFICIAL_ENV = {
    "NX30": "BOOTBRIDGE_NX30_R012_OFFICIAL",
    "RC3000": "BOOTBRIDGE_RC3000_R013L01_OFFICIAL",
    "RT3000": "BOOTBRIDGE_RT3000_R015_OFFICIAL",
    "RW3000": "BOOTBRIDGE_RW3000_R012_OFFICIAL",
}
SOURCES = [
    "bridge_version.c", "bridge_oem_slot.c", "bridge_backup.c",
    "bridge_mtd_observer.c", "bridge_first_install.c",
    "bridge_root_binding.c", "bridge_install_identity.c",
]
BOOT_ID = "11111111-1111-4111-8111-111111111111"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def exact_official(product):
    variable = OFFICIAL_ENV[product]
    value = os.environ.get(variable)
    if not value:
        raise unittest.SkipTest(f"set {variable} to the exact ordinary OEM package")
    path = Path(value).expanduser()
    if not path.is_file():
        raise unittest.SkipTest(f"{variable} does not name an available ordinary file")
    raw = path.read_bytes()
    identity = build.identify_official(raw)
    if identity.get("model") != product:
        raise AssertionError(f"{variable} contains a different exact official product")
    return raw, identity, build.official_bootchain_components(raw, identity)


def official_kernel(official, identity):
    outer_bytes = struct.unpack_from(">I", official, 164)[0]
    nodes = vendor_fit.fdt_props(official[160:160 + outer_bytes])
    ubi_images = [props["data"] for path, props in nodes.items()
                  if path.startswith("/images/ubi-") and "data" in props]
    if len(ubi_images) != 1:
        raise AssertionError("exact official package lacks one embedded UBI image")
    with tempfile.TemporaryDirectory(prefix="bootbridge-install-profile-ubi-") as td:
        path = Path(td) / "stock.ubi"
        path.write_bytes(ubi_images[0])
        _, volumes, _, _ = vendor_ubi.parse(path)
    if not isinstance(volumes.get("kernel"), bytes):
        raise AssertionError("exact official UBI lacks kernel volume")
    return volumes["kernel"]


def official_ubi(official):
    outer_bytes = struct.unpack_from(">I", official, 164)[0]
    nodes = vendor_fit.fdt_props(official[160:160 + outer_bytes])
    ubi_images = [props["data"] for path, props in nodes.items()
                  if path.startswith("/images/ubi-") and "data" in props]
    if len(ubi_images) != 1:
        raise AssertionError("exact official package lacks one embedded UBI image")
    return ubi_images[0]


def make_env(product, machid):
    raw = bytearray(b"\xff" * 0x80000)
    values = [f"productname={product}", f"machid={machid:08x}",
              "bootcmd=bootipq",
              "bootargs=console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"]
    body = b"\0".join(value.encode("ascii") for value in values) + b"\0\0"
    raw[4:4 + len(body)] = body
    crc = zlib.crc32(raw[4:0x40000]) & 0xffffffff
    struct.pack_into("<I", raw, 0, crc)
    return bytes(raw)


def extract_catalog(archive, destination):
    with tarfile.open(archive, "r:gz") as tf:
        members = [m for m in tf.getmembers() if m.name == PUBLIC_CATALOG_MEMBER]
        if len(members) != 1 or not members[0].isfile():
            raise AssertionError("pinned public archive has no unique regular NAND catalog")
        stream = tf.extractfile(members[0])
        if stream is None:
            raise AssertionError("cannot read public NAND catalog member")
        data = stream.read()
    destination.write_bytes(data)
    return data


def compile_and_run(board_class, temp, profile, candidate, other_candidate,
                    sbl_padded, appsbl_padded, env):
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not cc:
        raise unittest.SkipTest("host C compiler is unavailable")
    project_src = PROJECT / "src"
    include = temp / "include"
    include.mkdir()

    catalog_path = include / "h3c_nand_catalog.h"
    catalog_bytes = extract_catalog(PUBLIC_ARCHIVE, catalog_path)
    catalog = build.public_catalog_binding(
        {"nand_catalog_header_path": str(catalog_path),
         "nand_catalog_sha256": sha(catalog_bytes)},
        {"nand_profile_id": build.PUBLIC_BOARD_INPUTS[board_class]["nand_profile_id"]}, cc)

    payload, descriptor = build.public_candidate_from_archive(PUBLIC_ARCHIVE, board_class)
    official_product = profile["product_name"]
    official, identity, components = exact_official(official_product)
    if components["appsbl_padded_sha256"] != sha(appsbl_padded):
        raise AssertionError("synthetic active APPSBL is not the exact padded official component")

    stock = official_kernel(official, identity)
    bridge, _ = bridge_kernel.transform_kernel(stock, identity["sha256"])
    route_metadata = return_source.metadata(stock, bridge, identity["sha256"], route=profile["route"])
    source_header = return_source.c_header(route_metadata)
    (include / "bridge_oem_return_source.h").write_text(source_header)

    generated_profile = build.generated_first_install_profile_header(
        board_class, identity, components)
    (include / "bridge_first_install_profile.h").write_text(generated_profile)
    root_contract = root_binding_contract.derive(identity["sha256"], official_ubi(official))
    (include / root_binding_contract.HEADER_NAME).write_text(
        root_binding_contract.render_header(root_contract))
    payload_header = build.generated_public_payload_header(
        project_src / "bridge_payload.h", payload, descriptor, catalog, components,
        dict(profile, **{"class": board_class,
                         "source_sha256": identity["sha256"],
                         "product_id": return_source.PRODUCTS[identity["sha256"]]}))
    (include / "bridge_payload.h").write_text(payload_header)

    # The harness never runs code that consumes the OEM measurement manifest;
    # retain the production include/type path with an inert object.
    (include / "bridge_oem_manifest.h").write_text(
        '#include "bridge_oem_slot.h"\n'
        'static const bos_manifest bootbridge_oem_manifest = {0};\n')

    root = temp / "fixture-root"
    for rel, value in {
        "proc/sys/kernel/random/boot_id": BOOT_ID + "\n",
        "sys/devices/system/qfprom/qfprom0/authenticate": "0\n",
        "proc/cmdline": f"machid={profile['machid']:x} console=ttyMSM0,115200n8\n",
    }.items():
        path = root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)

    payload_path = temp / "candidate.partition.bin"
    sbl_path = temp / "sbl.partition.padded.bin"
    appsbl_path = temp / "official.appsbl.padded.bin"
    env_path = temp / "env.synthetic.bin"
    other_candidate_path = temp / "other-board-candidate.partition.bin"
    for path, data in ((payload_path, candidate), (sbl_path, sbl_padded),
                       (appsbl_path, appsbl_padded), (env_path, env),
                       (other_candidate_path, other_candidate)):
        path.write_bytes(data)

    executable = temp / "compiled-install-profile-harness"
    command = [cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
        '-DBI_FIRST_INSTALL_PROFILE_HEADER="bridge_first_install_profile.h"',
        '-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"',
        '-DBI_OEM_MANIFEST_HEADER="bridge_oem_manifest.h"',
        root_binding_contract.COMPILE_FLAG,
        "-I", str(include), "-I", str(project_src),
        str(PROJECT / "tests/compiled_install_profile_harness.c"),
        *[str(project_src / item) for item in SOURCES], "-lz", "-o", str(executable)]
    compiled = subprocess.run(command, check=False, capture_output=True, text=True, timeout=120)
    if compiled.returncode:
        raise AssertionError(f"production -Werror compile failed: {compiled.stderr.strip()}")
    result = subprocess.run([str(executable), str(root), str(payload_path), str(sbl_path),
                             str(appsbl_path), str(env_path), str(other_candidate_path)], check=False,
                            capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise AssertionError(f"synthetic qualifier failed ({result.returncode}): {result.stderr.strip()}")
    return result.stdout.strip()


class CompiledInstallProfileTests(unittest.TestCase):
    def test_each_public_board_compiles_and_matches_synthetic_observations(self):
        if not PUBLIC_ARCHIVE.is_file():
            self.skipTest("pinned ABCD public candidate archive is unavailable")
        requested = os.environ.get("BOOTBRIDGE_PROFILE_CLASSES", "ABCD")
        if not requested or any(board not in build.PUBLIC_BOARD_INPUTS for board in requested):
            self.fail("BOOTBRIDGE_PROFILE_CLASSES must be a nonempty subset of ABCD")
        for board_class in requested:
            public_profile = build.PUBLIC_BOARD_INPUTS[board_class]
            with self.subTest(board_class=board_class):
                official, identity, components = exact_official(public_profile["product_name"])
                if identity["sha256"] not in return_source.PRODUCTS:
                    self.fail("official source is not mapped by the public source metadata")
                payload, _ = build.public_candidate_from_archive(PUBLIC_ARCHIVE, board_class)
                other_board = "B" if board_class == "A" else "A"
                other_payload, _ = build.public_candidate_from_archive(PUBLIC_ARCHIVE, other_board)
                # Extract exact component bytes, then add only the documented
                # all-FF partition padding used by the compiled profile.
                outer_bytes = struct.unpack_from(">I", official, 164)[0]
                outer_nodes = vendor_fit.fdt_props(official[160:160 + outer_bytes])
                sbl_data = [props["data"] for props in outer_nodes.values()
                            if props.get("description") == b"sbl1_nand.mbn.padded\0"]
                appsbl_data = [props["data"] for props in outer_nodes.values()
                               if props.get("description") == b"openwrt-ipq5018-u-boot.mbn.padded\0"]
                self.assertEqual(len(sbl_data), 1)
                self.assertEqual(len(appsbl_data), 1)
                sbl_padded = sbl_data[0] + b"\xff" * (0x80000 - len(sbl_data[0]))
                appsbl_padded = appsbl_data[0] + b"\xff" * (build.APPSBL_BYTES - len(appsbl_data[0]))
                env = make_env(public_profile["product_name"], public_profile["machid"])
                with tempfile.TemporaryDirectory(prefix=f"bootbridge-profile-{board_class}-") as td:
                    output = compile_and_run(board_class, Path(td), public_profile,
                                             payload, other_payload, sbl_padded,
                                             appsbl_padded, env)
                self.assertIn("PASS production compiled-profile qualifier", output)
                self.assertIn("all observations synthetic", output)


if __name__ == "__main__":
    unittest.main(verbosity=2)
