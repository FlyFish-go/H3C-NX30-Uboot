#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run pinned fresh-candidate coverage and compiled RC/HW-C qualification.

The archive, official BIN, and catalog bytes are real pinned inputs. The final
production qualifier receives synthetic read-only observations; this is a
software regression only and grants no hardware qualification or install-ready
status.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tarfile
import tempfile
import zlib

PROJECT = Path(__file__).resolve().parents[1]
REPO = PROJECT.parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
sys.path.insert(0, str(PROJECT / "tests"))

import bridge_kernel
import build
import build_return_source as return_source
import candidate_manifest
import candidate_profile_codegen
import nand_schema
import test_compiled_install_profile as legacy_harness
import vendor_fit

PROFILE_ID = "h3c-oem-efba-128m-2k-sp64"
VARIANTS = ("A0", "A1", "B", "D", "C")
RC_VARIANTS = ("A0", "C")
SOURCES = ["bridge_version.c", "bridge_oem_slot.c", "bridge_backup.c",
           "bridge_mtd_observer.c", "bridge_first_install.c",
           "bridge_root_binding.c", "bridge_install_identity.c"]


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def require_sha(path: Path, expected: str, label: str) -> bytes:
    raw = path.read_bytes()
    if sha(raw) != expected:
        raise ValueError(f"{label} SHA-256 does not match the caller pin")
    return raw


def archive_catalog(archive_path: Path) -> bytes:
    with tarfile.open(archive_path, "r:gz") as tf:
        names = [m for m in tf.getmembers() if m.isfile() and
                 m.name.startswith("h3c-uboot-candidates-v2/source/") and
                 m.name.endswith("/h3c_nand_catalog.h")]
        if len(names) != 1:
            raise ValueError("pinned archive must contain one exact source NAND catalog")
        stream = tf.extractfile(names[0])
        if stream is None:
            raise ValueError("cannot read archived source NAND catalog")
        return stream.read()


def validate_source_catalog_profile(profile: dict, storage_ecc_bits: int,
                                    source_mask: int) -> None:
    if storage_ecc_bits not in (4, 8):
        raise ValueError("candidate storage ECC is outside the source catalog")
    ecc_mode = 1 if storage_ecc_bits == 4 else 2
    ecc_sources = (profile["appsbl_ecc4_source_mask"] if storage_ecc_bits == 4
                   else profile["appsbl_ecc8_source_mask"])
    if (profile["qpic_capacity_mib"] not in (128, 256, 512) or
            not profile["qpic_ecc_mode_mask"] & ecc_mode or
            not profile["appsbl_ecc_mode_mask"] & ecc_mode or
            not profile["appsbl_source_mask"] & source_mask or
            not ecc_sources & profile["appsbl_source_mask"] & source_mask):
        raise ValueError("candidate lacks exact APPSBL/source-format/QPIC capacity+ECC support")


def coverage(parsed: dict, archive_path: Path,
             catalog_bytes: bytes) -> tuple[list[dict], dict[str, list[dict]]]:
    rows = parsed["candidates"]
    if len(rows) != 84:
        raise ValueError(f"expected 84 archive candidate rows, found {len(rows)}")
    with tempfile.TemporaryDirectory(prefix="fresh-candidate-catalog-") as td:
        catalog_path = Path(td) / "h3c_nand_catalog.h"
        catalog_path.write_bytes(catalog_bytes)
        catalog = nand_schema.profiles(catalog_path)
    catalog_by_id = {p["profile_id"]: p for p in catalog["profiles"]}
    runtime = nand_schema.runtime_profiles(VARIANTS, 15, catalog)
    source_mask = sum(nand_schema.TABLE_MASKS.values())
    by_candidate: dict[str, list[dict]] = {}
    ids = set()
    for row in rows:
        if row["candidate_id"] in ids:
            raise ValueError("duplicate candidate ID in parsed archive")
        ids.add(row["candidate_id"])
        if row["hardware_validation"] != "NOT_EXECUTED":
            raise ValueError("candidate archive hardware status is not NOT_EXECUTED")
        profile = catalog_by_id.get(row["nand_profile_id"])
        if profile is None:
            raise ValueError(f"candidate {row['candidate_id']} profile is absent from the source catalog")
        try:
            validate_source_catalog_profile(profile, row["storage_ecc_bits"], source_mask)
        except ValueError as exc:
            raise ValueError(f"candidate {row['candidate_id']}: {exc}") from exc
        matches = [p for p in runtime if p["profile_id"] == row["nand_profile_id"] and
                   p["ecc_bits"] == row["storage_ecc_bits"] and
                   p["catalogue_eligible"] & (1 << "ABCD".index(row["board"]))]
        by_candidate[row["candidate_id"]] = matches
        elf = candidate_manifest.extract_candidate_member_v2(archive_path, row, "appsbl.elf")
        bss_start, bss_end = candidate_profile_codegen.elf_bss_symbols(elf)
        bss = row["bss_metadata"]
        if (bss.get("elf_sha256") != row["files"]["appsbl.elf"]["sha256"] or
                bss.get("bss_start") != bss_start or bss.get("bss_end") != bss_end or
                bss.get("bss_bytes") != bss_end-bss_start or
                not row["container"]["entry"] <= bss_start < bss_end <= bss.get("sdram_limit", 0)):
            raise ValueError(f"candidate {row['candidate_id']} BSS facts disagree with exact ELF symbols")
    return rows, by_candidate


def make_env(product: str, machid: int) -> bytes:
    raw = bytearray(b"\xff" * 0x80000)
    values = [f"productname={product}", f"machid={machid:08x}",
              "bootcmd=bootipq",
              "bootargs=console=ttyMSM0,115200n8 cnss2.skip_radio_bmap=4 loglevel=3"]
    body = b"\0".join(value.encode("ascii") for value in values) + b"\0\0"
    raw[4:4 + len(body)] = body
    struct.pack_into("<I", raw, 0, zlib.crc32(raw[4:0x40000]) & 0xffffffff)
    return bytes(raw)


def negative_codegen_checks(candidate: dict, elf: bytes, tuples: list[dict],
                            profile: dict, bss: dict) -> None:
    wrong = dict(candidate, board=("D" if candidate["board"] != "D" else "C"))
    try:
        candidate_profile_codegen.render_headers(wrong, elf, tuples, profile, bss)
    except candidate_profile_codegen.ProfileCodegenError:
        pass
    else:
        raise AssertionError("wrong-board/source candidate was accepted")
    try:
        candidate_profile_codegen.render_headers(candidate, elf, tuples + tuples,
            dict(profile, mtd_oob_bytes=[tuples[0]["mtd_oob_bytes"]]), bss)
    except candidate_profile_codegen.ProfileCodegenError:
        pass
    else:
        raise AssertionError("duplicate/ambiguous exact OOB tuple was accepted")


def run_qualifier(args, archive, descriptor, tuple_rows, board_class, official, identity,
                  components, catalog_bytes) -> str:
    if identity.get("model") != "RC3000" or identity["sha256"] not in return_source.PRODUCTS:
        raise ValueError("--official must be a pinned ordinary RC3000 package")
    build_profile = build.candidate_build_profile(descriptor, board_class, "RC3000", tuple_rows)
    machid = build_profile["machid"]
    if isinstance(machid, str):
        machid = int(machid, 16)
    if machid != build.PUBLIC_BOARD_INPUTS[board_class]["machid"]:
        raise ValueError(f"archived HW-{board_class} machid differs from reviewed board build input")
    # The packed U-Boot DT and the OEM Linux return-route DT describe distinct
    # artifacts. Keep the archive-bound U-Boot value in the candidate profile;
    # the OEM route is independently derived from the pinned stock kernel below.
    if not isinstance(build_profile.get("dt_compatible"), str) or not build_profile["dt_compatible"]:
        raise ValueError(f"archived HW-{board_class} U-Boot DT compatible is missing")
    build_profile["mtd_oob_bytes"] = sorted({r["mtd_oob_bytes"] for r in tuple_rows})
    nand_tuple = dict(tuple_rows[0], accepted_mtd_oob_mask=sum(
        {64: 1, 128: 2, 256: 4}[r["mtd_oob_bytes"]] for r in tuple_rows))
    elf = candidate_manifest.extract_candidate_member_v2(
        args.archive, descriptor, "appsbl.elf")
    bfi_header, bii_header = candidate_profile_codegen.render_headers(
        descriptor, elf, tuple_rows, build_profile, descriptor["bss_metadata"])

    profile = dict(build.PUBLIC_BOARD_INPUTS[board_class], **{
        "class": board_class, "product_name": "RC3000", "source_sha256": identity["sha256"],
        "nand_profile_id": descriptor["nand_profile_id"], "nand_profile": nand_tuple,
        "nand_profiles": tuple_rows, "accepted_mtd_oob_mask": nand_tuple["accepted_mtd_oob_mask"],
        "ecc": descriptor["storage_ecc_bits"], "load_ecc": descriptor["load_ecc_bits"],
        "candidate_id": descriptor["candidate_id"], "record": descriptor["candidate_id"],
        "model": nand_tuple["model_aliases"][0], "manufacturer": nand_tuple["manufacturer_id"],
        "chip": nand_tuple["chip_id"], "raw_spare": nand_tuple["raw_spare_bytes"],
        "mtd_oob": nand_tuple["mtd_oob_bytes"], "capacity": nand_tuple["capacity_mib"],
        "product_id": return_source.PRODUCTS[identity["sha256"]]})
    profile_header = build.generated_first_install_profile_header(
        board_class, identity, components, nand_profile=nand_tuple,
        candidate_id=descriptor["candidate_id"])
    payload = candidate_manifest.extract_candidate_member_v2(
        args.archive, descriptor, "appsbl.partition.bin")
    mbn = candidate_manifest.extract_candidate_member_v2(args.archive, descriptor, "appsbl.mbn")
    if payload[:len(mbn)] != mbn or payload[len(mbn):] != b"\xff" * (len(payload)-len(mbn)):
        raise ValueError("actual archive partition no longer exactly pads the actual MBN")

    outer_len = struct.unpack_from(">I", official, 164)[0]
    outer = vendor_fit.fdt_props(official[160:160 + outer_len])
    ubi_images = [p["data"] for name, p in outer.items()
                  if name.startswith("/images/ubi-") and "data" in p]
    if len(ubi_images) != 1:
        raise ValueError("pinned RC package must contain exactly one embedded UBI image")
    stock_kernel = legacy_harness.official_kernel(official, identity)
    transformed, _ = bridge_kernel.transform_kernel(stock_kernel, identity["sha256"])
    route_meta = return_source.metadata(stock_kernel, transformed, identity["sha256"],
                                        route=build.PUBLIC_BOARD_INPUTS[board_class]["route"])
    source_header = return_source.c_header(route_meta)
    cc = shutil.which(os.environ.get("CC", "cc"))
    if not cc:
        raise RuntimeError("host C compiler is unavailable")
    project_src = PROJECT / "src"
    with tempfile.TemporaryDirectory(prefix=f"fresh-candidate-RC-HW{board_class}-") as tmp:
        temp = Path(tmp)
        include = temp / "include"
        include.mkdir()
        (include / "bfi_candidate_profile.generated.h").write_text(bfi_header, encoding="ascii")
        (include / "bii_candidate_policy.generated.h").write_text(bii_header, encoding="ascii")
        (include / "bridge_first_install_profile.h").write_text(profile_header, encoding="ascii")
        (include / "bridge_oem_return_source.h").write_text(source_header, encoding="ascii")
        (include / "bridge_oem_manifest.h").write_text(
            '#include "bridge_oem_slot.h"\nstatic const bos_manifest bootbridge_oem_manifest = {0};\n',
            encoding="ascii")
        catalog_path = include / "h3c_nand_catalog.h"
        catalog_path.write_bytes(catalog_bytes)
        catalog_binding = build.public_catalog_binding(
            {"nand_catalog_header_path": str(catalog_path),
             "nand_catalog_sha256": sha(catalog_bytes)}, profile, cc)
        (include / "bridge_payload.h").write_text(build.generated_public_payload_header(
            project_src / "bridge_payload.h", payload, descriptor, catalog_binding,
            components, profile), encoding="ascii")
        root = temp / "fixture-root"
        for rel, value in {
            "proc/sys/kernel/random/boot_id": legacy_harness.BOOT_ID + "\n",
            "sys/devices/system/qfprom/qfprom0/authenticate": "0\n",
            "proc/cmdline": f"machid={machid:x} console=ttyMSM0,115200n8\n",
        }.items():
            path = root / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(value)
        payload_path = temp / "candidate.partition.bin"
        sbl_path = temp / "sbl.partition.padded.bin"
        appsbl_path = temp / "official.appsbl.padded.bin"
        env_path = temp / "env.synthetic.bin"
        other_path = temp / "other.candidate.partition.bin"
        other = candidate_manifest.extract_candidate_member_v2(
            args.archive, args.other_candidate, "appsbl.partition.bin")
        sbl_data = [p["data"] for p in outer.values()
                    if p.get("description") == b"sbl1_nand.mbn.padded\0"]
        appsbl_data = [p["data"] for p in outer.values()
                       if p.get("description") == b"openwrt-ipq5018-u-boot.mbn.padded\0"]
        if len(sbl_data) != 1 or len(appsbl_data) != 1:
            raise ValueError("pinned RC package lacks one exact SBL/APPSBL pair")
        sbl_pad = sbl_data[0] + b"\xff" * (0x80000-len(sbl_data[0]))
        appsbl_pad = appsbl_data[0] + b"\xff" * (build.APPSBL_BYTES-len(appsbl_data[0]))
        for path, data in ((payload_path, payload), (sbl_path, sbl_pad),
                           (appsbl_path, appsbl_pad), (env_path, make_env("RC3000", machid)),
                           (other_path, other)):
            path.write_bytes(data)
        executable = temp / "compiled-install-profile-harness"
        command = [cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
            '-DBFI_PROFILE_RECORD_HEADER="bfi_candidate_profile.generated.h"',
            '-DBII_CANDIDATE_POLICY_HEADER="bii_candidate_policy.generated.h"',
            '-DBI_FIRST_INSTALL_PROFILE_HEADER="bridge_first_install_profile.h"',
            '-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"',
            '-DBI_OEM_MANIFEST_HEADER="bridge_oem_manifest.h"',
            "-I", str(include), "-I", str(project_src),
            str(PROJECT / "tests/compiled_install_profile_harness.c"),
            *[str(project_src / name) for name in SOURCES], "-lz", "-o", str(executable)]
        compiled = __import__("subprocess").run(command, capture_output=True, text=True, timeout=120)
        if compiled.returncode:
            raise RuntimeError("production -Werror compile failed: " + compiled.stderr.strip())
        run = __import__("subprocess").run(
            [str(executable), str(root), str(payload_path), str(sbl_path),
             str(appsbl_path), str(env_path), str(other_path)],
            capture_output=True, text=True, timeout=30)
        if run.returncode:
            raise RuntimeError(f"full production qualifier rejected synthetic facts: {run.stderr.strip()}")
        return run.stdout.strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--archive-sha256", required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--candidates-sha256", required=True)
    parser.add_argument("--official", type=Path, required=True,
                        help="exact ordinary RC3000 OEM BIN; never a bridge package")
    parser.add_argument("--official-sha256", required=True)
    parser.add_argument("--source-variants", default=",".join(VARIANTS),
                        help="all five variants used for 84-row archive coverage")
    args = parser.parse_args()
    variants = tuple(args.source_variants.split(","))
    if set(variants) != set(VARIANTS):
        raise ValueError("all five reviewed OEM table variants must be included for 84-row coverage")
    parsed = candidate_manifest.read_candidate_archive_v2(args.archive,
        args.archive_sha256, args.manifest_sha256, args.candidates_sha256)
    catalog_bytes = archive_catalog(args.archive)
    rows, matched = coverage(parsed, args.archive, catalog_bytes)
    rc_runtime = nand_schema.runtime_profiles(RC_VARIANTS, 15)
    by_key: dict[tuple[str, str, int], list[dict]] = {}
    for row in rows:
        by_key.setdefault((row["board"], row["nand_profile_id"], row["storage_ecc_bits"]), []).append(row)
    eligible: list[tuple[dict, list[dict]]] = []
    ineligible: list[dict] = []
    expected_keys = set()
    for board in "ABCD":
        bit = 1 << "ABCD".index(board)
        for profile in rc_runtime:
            if profile["catalogue_eligible"] & bit:
                expected_keys.add((board, profile["profile_id"], profile["ecc_bits"]))
    for row in rows:
        key = (row["board"], row["nand_profile_id"], row["storage_ecc_bits"])
        source_rows = [p for p in rc_runtime if p["profile_id"] == row["nand_profile_id"] and
                       p["ecc_bits"] == row["storage_ecc_bits"] and
                       p["catalogue_eligible"] & (1 << "ABCD".index(row["board"]))]
        if not source_rows:
            ineligible.append({"candidate_id": row["candidate_id"], "board": row["board"],
                "profile_id": row["nand_profile_id"], "storage_ecc_bits": row["storage_ecc_bits"],
                "reason": "no exact RC source-table/catalog runtime profile for this board/profile/ECC"})
            continue
        archive_rows = matched[row["candidate_id"]]
        archive_oobs = {p["mtd_oob_bytes"] for p in archive_rows
                        if p["profile_id"] == row["nand_profile_id"] and
                        p["ecc_bits"] == row["storage_ecc_bits"]}
        if not {p["mtd_oob_bytes"] for p in source_rows}.issubset(archive_oobs):
            raise ValueError(f"RC exact tuple is absent from catalog coverage for {row['candidate_id']}")
        eligible.append((row, source_rows))
    for key in expected_keys:
        if len(by_key.get(key, [])) != 1:
            raise ValueError(f"RC source-eligible board/profile/ECC row is missing or ambiguous: {key}")
    if len(eligible) != 68 or len(ineligible) != 16:
        raise ValueError(f"expected 68 RC-eligible and 16 source-ineligible archive rows; found {len(eligible)}/{len(ineligible)}")
    official = require_sha(args.official, args.official_sha256, "official RC BIN")
    identity = build.identify_official(official)
    if identity.get("model") != "RC3000":
        raise ValueError("official BIN identity is not RC3000")
    components = build.official_bootchain_components(official, identity)
    qualifier_results = []
    named_fault_result = None
    for descriptor, tuple_rows in eligible:
        board = descriptor["board"]
        elf = candidate_manifest.extract_candidate_member_v2(args.archive, descriptor, "appsbl.elf")
        profile = build.candidate_build_profile(descriptor, board, "RC3000", tuple_rows)
        negative_codegen_checks(descriptor, elf, tuple_rows, profile, descriptor["bss_metadata"])
        args.other_candidate = next(r for r in rows if r["candidate_id"] != descriptor["candidate_id"])
        output = run_qualifier(args, args.archive, descriptor, tuple_rows, board,
                               official, identity, components, catalog_bytes)
        qualifier_results.append({"candidate_id": descriptor["candidate_id"], "board": board,
            "profile_id": descriptor["nand_profile_id"], "load_ecc_bits": descriptor["load_ecc_bits"],
            "storage_ecc_bits": descriptor["storage_ecc_bits"],
            "mtd_oob_bytes": sorted({p["mtd_oob_bytes"] for p in tuple_rows}), "result": output})
        if (board == "C" and descriptor["nand_profile_id"] == PROFILE_ID and
                descriptor["storage_ecc_bits"] == 4 and
                "W25N01GWZEIG" in tuple_rows[0]["model_aliases"]):
            named_fault_result = descriptor["candidate_id"]
    if named_fault_result is None:
        raise ValueError("named RC HW-C mp03.1-c2 W25N01GWZEIG regression row is absent")
    report = {
        "schema": "fresh-candidate-profile-regression-v1",
        "archive_sha256": parsed["archive_sha256"],
        "manifest_sha256": parsed["manifest_sha256"],
        "candidates_sha256": parsed["candidates_sha256"],
        "source_snapshot_sha256": parsed["source_snapshot_sha256"],
        "candidate_rows_covered": len(rows),
        "candidate_board_counts": {b: sum(1 for r in rows if r["board"] == b) for b in "ABCD"},
        "source_eligible_candidate_rows": len(qualifier_results),
        "source_ineligible_candidate_rows": len(ineligible),
        "source_ineligible_reasons": ineligible,
        "named_fault_regression_candidate_id": named_fault_result,
        "official_rc_sha256": identity["sha256"], "compiled_qualifier_results": qualifier_results,
        "result": "SOFTWARE_ONLY_PASS for source-eligible rows; synthetic observations; no device I/O or writes",
        "hardware_validation": "NOT_EXECUTED",
    }
    print(json.dumps(report, sort_keys=True, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, candidate_manifest.CandidateError,
            candidate_profile_codegen.ProfileCodegenError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        raise SystemExit(2)
