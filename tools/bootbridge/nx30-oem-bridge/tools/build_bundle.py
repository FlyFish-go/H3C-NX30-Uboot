#!/usr/bin/env python3
"""Offline builder for one source-hash-bound, multi-provider OEM bundle."""
if not __debug__:
    raise RuntimeError("bundle builder requires assertions")

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
from types import SimpleNamespace

import build
import candidate_manifest
import nand_schema
import vendor_ubi
import root_binding_contract


SOURCE_BOARDS = {
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": ("A", "B", "C", "D"),
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": ("A", "B", "C", "D"),
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": ("A", "B", "C", "D"),
    "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922": ("A", "B", "C", "D"),
}
SOURCE_TABLE_VARIANTS = {source_sha: ("A0", "C") for source_sha in SOURCE_BOARDS}
SOURCE_PRODUCT_COVERAGE = {
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": {
        "RC3000": ("A", "B", "C", "D")},
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": {
        "RT3000": ("A", "B", "C", "D")},
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": {
        "RW3000": ("A", "B", "C", "D")},
    # Exact NX R012 bytes are also bound to the identified RX R012 source.
    # The RX OEM environment admits A-D; NX identity admits only D.
    "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922": {
        "NX30": ("D",), "RX3000": ("A", "B", "C", "D")},
}
PRODUCT_IDENTITY_EVIDENCE = {
    ("61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "RX3000"):
        {"path": "source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json",
         "bytes": 13544,
         "sha256": "9834e1b2b92b59d4822905fc5ebe9096462e44106534b440598c85a67c2bf4c6"},
}
PROVIDER_PATH = "/usr/sbin/bridge-check-HW-{board}"
PAYLOAD_PATH = "/etc/bridge/providers/HW-{board}/uboot.bin"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def regular(path):
    return build.regular(Path(path))


def source_revision_binding(descriptor):
    """Hash an archive-bound candidate revision identity (not FIT return revision)."""
    files = descriptor.get("files") if isinstance(descriptor, dict) else None
    if not isinstance(files, dict):
        raise ValueError("candidate source-revision descriptor missing files")
    mbn = files.get("appsbl.mbn", {})
    partition = files.get("appsbl.partition.bin", {})
    fresh = descriptor.get("source_snapshot_sha256")
    binding = {
        "schema": "bootbridge-candidate-source-snapshot-v2" if fresh else "bootbridge-candidate-source-revision-v1",
        "archive_sha256": descriptor.get("archive_sha256", candidate_manifest.ARCHIVE_SHA256),
        "manifest_sha256": descriptor.get("manifest_sha256", candidate_manifest.MANIFEST_SHA256),
        "candidates_sha256": descriptor.get("candidates_sha256", candidate_manifest.CANDIDATES_SHA256),
        "board": descriptor.get("board"),
        "candidate_id": descriptor.get("candidate_id"),
        "source_snapshot_sha256" if fresh else "source_revision_git": fresh or descriptor.get("source_revision"),
        "appsbl_mbn_sha256": mbn.get("sha256"),
        "appsbl_partition_sha256": partition.get("sha256"),
    }
    if (binding["board"] not in ("A", "B", "C", "D") or
            not isinstance(binding["source_snapshot_sha256" if fresh else "source_revision_git"], str) or
            len(binding["source_snapshot_sha256" if fresh else "source_revision_git"]) != (64 if fresh else 40) or
            any(not isinstance(binding[k], str) or len(binding[k]) != 64 or
                any(ch not in "0123456789abcdef" for ch in binding[k])
                for k in ("archive_sha256", "manifest_sha256", "candidates_sha256",
                          "appsbl_mbn_sha256", "appsbl_partition_sha256")) or
            any(c not in "0123456789abcdef" for c in binding["source_snapshot_sha256" if fresh else "source_revision_git"])):
        raise ValueError("candidate source-revision binding fields are malformed")
    canonical = json.dumps(binding, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=True).encode("ascii")
    return binding, sha(canonical)


def source_boards(identity):
    source = identity.get("sha256")
    if source not in SOURCE_BOARDS or source not in build.OFFICIAL_SOURCES:
        raise ValueError("exact official source has no reviewed provider bundle")
    expected_product = {
        "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": "RC3000",
        "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": "RT3000",
        "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": "RW3000",
        "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922": "NX30",
    }[identity["sha256"]]
    if identity.get("model") != expected_product:
        raise ValueError("exact official source and recognized product disagree")
    # The exact R012 package bytes are shared with RX3000. Package coverage
    # includes all provider rows; install_identity separately admits NX30 only
    # on D and RX only on its evidence-bound A/B/C/D product identity.
    return SOURCE_BOARDS[source]


def source_nand_profile_set(identity):
    """Return exact source-bound profile inventory from OEM A0/C tables."""
    source = identity.get("sha256")
    variants = SOURCE_TABLE_VARIANTS.get(source)
    if variants is None:
        raise ValueError("exact official source has no audited APPSBL NAND-table mapping")
    set_id, profile_ids = nand_schema.profile_set_id(source, variants)
    catalog = nand_schema.profiles()
    rows = nand_schema.runtime_profiles(variants, candidate_board_mask=0, catalog=catalog)
    by_id = {row["profile_id"]: row for row in catalog["profiles"]}
    inventory = [dict(by_id[pid]) for pid in profile_ids]
    runtime = [row for row in rows if row["profile_id"] in set(profile_ids)]
    return {"profile_set_id": set_id, "source_sha256": source,
            "catalog_sha256": catalog["catalog_sha256"],
            "source_variants": list(variants), "profile_ids": profile_ids,
            "profiles": inventory, "runtime_profiles": runtime}


def candidate_archive_format_set(catalog_rows, boards):
    """Full immutable U-Boot/APPSBL matrix; runtime/kernel exclusions are
    applied later and do not shrink the 21-format archive contract.
    """
    all_table_mask = sum(nand_schema.TABLE_MASKS.values())
    result = set()
    for profile in catalog_rows.values():
        formats = [ecc for ecc, bit, field in ((4, 1, "appsbl_ecc4_source_mask"),
                                                (8, 2, "appsbl_ecc8_source_mask"))
                   if profile["appsbl_ecc_mode_mask"] & bit and
                      profile["runtime_ecc_mode_mask"] & bit and profile[field] & all_table_mask]
        for board in boards:
            for ecc in formats:
                result.add((board, profile["profile_id"], ecc, ecc))
    return result


def supported_product_identities(source_sha256):
    coverage = SOURCE_PRODUCT_COVERAGE.get(source_sha256)
    if coverage is None:
        raise ValueError("exact official source has no reviewed product identity mapping")
    rows = []
    for product, classes in coverage.items():
        row = {"product": product, "source_sha256": source_sha256,
               "hardware_classes": list(classes)}
        evidence = PRODUCT_IDENTITY_EVIDENCE.get((source_sha256, product))
        if evidence is not None:
            row["identity_evidence"] = dict(evidence)
        rows.append(row)
    return rows


def official_version(identity):
    """Return the exact model/version token already validated from the OEM header."""
    token = identity.get("identity")
    model = identity.get("model")
    if not isinstance(token, str) or not isinstance(model, str) or not token.startswith(model + "V"):
        raise ValueError("exact official identity has no canonical product/version token")
    return token


def provider_material(board, identity, provider_root, candidate_set_id=None):
    report_path = provider_root / "BUILD-REPORT.json"
    report = json.loads(regular(report_path))
    if (report.get("status") != "OFFLINE_LAB_CANDIDATE" or
            report.get("hardware_class") != board or
            report.get("source_official_sha256") != identity["sha256"]):
        raise ValueError("single-provider build report identity mismatch")
    root_binding = report.get("root_binding_contract")
    root_header = provider_root / "bridge_root_binding_contract.generated.h"
    if (not isinstance(root_binding, dict) or not root_header.is_file() or root_header.is_symlink() or
            root_binding.get("path") != "bridge_root_binding_contract.generated.h" or
            root_binding.get("sha256") != sha(regular(root_header)) or
            regular(root_header) != root_binding_contract.render_header(root_binding.get("contract", {})).encode("ascii")):
        raise ValueError("provider rootfs geometry contract/header is absent or inconsistent")
    descriptor = report.get("public_candidate_descriptor")
    route = build.PUBLIC_BOARD_INPUTS[board]["route"]
    ret = report.get("return_source")
    if (not isinstance(descriptor, dict) or not isinstance(ret, dict) or
            ret.get("route") != route or
            ret.get("compatible") != build.kernel_transform.ROUTES[route][1]):
        raise ValueError("provider candidate or exact FIT route is missing")
    slug = "c-" + hashlib.sha256(descriptor["candidate_id"].encode("utf-8")).hexdigest()[:20]
    provider_rel = f"usr/sbin/bridge-candidates/{slug}/provider"
    fresh_variant = "appsbl.elf" in descriptor.get("files", {})
    storage = report.get("payload_storage") if fresh_variant else None
    patch_meta = report.get("payload_patch") if fresh_variant else None
    if fresh_variant:
        if (not isinstance(storage, dict) or set(storage) != {
                "encoding", "path", "sha256", "bytes", "patch_header_sha256", "patch_count", "data_bytes"} or
                storage.get("encoding") != "base-patch-v1" or not isinstance(patch_meta, dict)):
            raise ValueError("fresh provider lacks exact base-patch storage metadata")
        base_sha = storage["sha256"]
        expected_payload_rel = f"etc/bridge/provider-payload-bases/{base_sha}.bin"
        if storage["path"] != "/" + expected_payload_rel:
            raise ValueError("fresh provider base path is not content-addressed")
        payload_rel = storage["path"].lstrip("/")
    else:
        payload_rel = f"etc/bridge/providers/{slug}/uboot.bin"
    provider_file = provider_root / "rootfs" / provider_rel
    payload_file = provider_root / "rootfs" / payload_rel
    for path, mode in ((provider_file, 0o555), (payload_file, 0o400)):
        st = path.lstat()
        if not path.is_file() or path.is_symlink() or (st.st_mode & 0o777) != mode:
            raise ValueError("provider or payload type/mode mismatch")
    profile = build.PUBLIC_BOARD_INPUTS[board]
    provider_build_rel = provider_root.relative_to(provider_root.parent.parent).as_posix()
    revision_binding, revision_sha256 = source_revision_binding(descriptor)
    runtime_profiles = descriptor.get("nand_profiles")
    if not isinstance(runtime_profiles, list) or not runtime_profiles:
        runtime_profile = descriptor.get("nand_profile")
        if isinstance(runtime_profile, dict):
            runtime_profiles = [runtime_profile]
    if not isinstance(runtime_profiles, list) or not runtime_profiles:
        # Frozen baseline archives have no per-profile descriptor; retain their
        # exact historical tuple while fresh v2 builds carry the normalized row.
        runtime_profile = next((p for p in nand_schema.runtime_profiles(("A0", "C"),
            {"A":1,"B":2,"C":4,"D":8}[board])
            if p["profile_id"] == descriptor.get("nand_profile_id", profile["nand_profile_id"]) and
               p["ecc_bits"] == descriptor.get("storage_ecc_bits", profile["ecc"])), None)
        runtime_profiles = [runtime_profile] if isinstance(runtime_profile, dict) else []
    if not runtime_profiles or any(not isinstance(p, dict) for p in runtime_profiles):
        raise ValueError("candidate descriptor lacks one exact source-compatible NAND profile")
    runtime_profiles = sorted(runtime_profiles, key=lambda p: p["mtd_oob_bytes"])
    if (len({p.get("mtd_oob_bytes") for p in runtime_profiles}) != len(runtime_profiles) or
            any(p.get("profile_id") != descriptor.get("nand_profile_id", runtime_profiles[0]["profile_id"]) or
                p.get("ecc_bits") != descriptor.get("storage_ecc_bits", runtime_profiles[0]["ecc_bits"])
                for p in runtime_profiles)):
        raise ValueError("candidate NAND tuple list is not one exact profile/storage ECC with unique OOBs")
    runtime_profile = runtime_profiles[0]
    aliases = runtime_profile["model_aliases"]
    nand = {
        "model": aliases[0], "model_aliases": aliases,
        "profile_id": runtime_profile["profile_id"], "jedec_bytes_observed": 2,
        "manufacturer_id": runtime_profile["manufacturer_id"], "chip_id": runtime_profile["chip_id"],
        "capacity_mib": runtime_profile["capacity_mib"], "page_bytes": runtime_profile["page_bytes"],
        "erase_bytes": runtime_profile["erase_bytes"],
        "raw_spare_bytes": runtime_profile["raw_spare_bytes"],
        "mtd_oob_bytes": runtime_profile["mtd_oob_bytes"], "ecc_bits": runtime_profile["ecc_bits"],
    }
    base_payload = payload_file.read_bytes()
    final_payload = descriptor["files"]["appsbl.partition.bin"]
    patch_header_bytes = (regular(provider_root / "bridge_payload_patch.h") if fresh_variant else None)
    if fresh_variant:
        if (len(base_payload) != storage["bytes"] or sha(base_payload) != storage["sha256"] or
                storage["patch_header_sha256"] != sha(patch_header_bytes) or
                patch_meta.get("candidate_id") != descriptor["candidate_id"] or
                patch_meta.get("candidate_sha256") != final_payload["sha256"] or
                patch_meta.get("candidate_bytes") != final_payload["bytes"] or
                patch_meta.get("base_sha256") != storage["sha256"] or
                patch_meta.get("base_bytes") != storage["bytes"] or
                patch_meta.get("patch_count") != storage["patch_count"] or
                patch_meta.get("patch_bytes") != storage["data_bytes"] or
                patch_meta.get("reconstructed_sha256") != final_payload["sha256"]):
            raise ValueError("provider base/patch facts differ from exact candidate and header pins")
    elif (len(base_payload) != final_payload["bytes"] or sha(base_payload) != final_payload["sha256"]):
        raise ValueError("provider payload differs from exact public candidate")
    payload_sha = final_payload["sha256"]
    provider_sha = sha(provider_file.read_bytes())
    record = {
        "record_id": "record-" + hashlib.sha256((board + ":" + descriptor["candidate_id"]).encode()).hexdigest(),
        "candidate_id": descriptor["candidate_id"], "hardware_class": board,
        "source_sha256": identity["sha256"], "product": identity["model"],
        "route": ret["route"], "route_compatible": ret["compatible"],
        "source_revision_sha256": revision_sha256,
        "source_revision_binding": revision_binding,
        "stock_kernel_sha256": ret["stock_kernel_sha256"],
        "bridge_kernel_sha256": ret["bridge_kernel_sha256"],
        "nand_dt_sha256": ret["nand_dt_sha256"], "nand": nand,
        "nand_profile_id": nand["profile_id"],
        "profile_set_id": report.get("profile_set_id", source_nand_profile_set(identity)["profile_set_id"]),
        "candidate_set_id": candidate_set_id or report.get("candidate_set_id", ""),
        "nand_profiles": runtime_profiles,
        "load_ecc_bits": descriptor.get("load_ecc_bits", profile["ecc"]),
        "storage_ecc_bits": descriptor.get("storage_ecc_bits", profile["ecc"]),
        "source_snapshot_sha256": descriptor.get("source_snapshot_sha256"),
        "provider": {"path": "/" + provider_rel, "sha256": provider_sha,
                     "bytes": provider_file.stat().st_size, "mode": 0o555},
        "payload": {"path": storage["path"], "sha256": payload_sha,
                    "bytes": final_payload["bytes"], "mode": 0o400,
                    "candidate_id": descriptor["candidate_id"], "descriptor": descriptor,
                    "mbn": descriptor["files"]["appsbl.mbn"],
                    "load": descriptor["container"]["load"]},
        "profile_header": {"path": provider_build_rel + "/bridge_first_install_profile.h",
                           "sha256": sha(regular(provider_root / "bridge_first_install_profile.h"))},
        "provider_report": {"path": provider_build_rel + "/BUILD-REPORT.json",
                            "sha256": sha(regular(report_path))},
        "return_source": ret,
        "root_binding_contract": root_binding["contract"],
        "root_binding_contract_header": {"path": provider_build_rel + "/bridge_root_binding_contract.generated.h",
            "sha256": root_binding["sha256"], "bytes": root_binding["header_bytes"]},
    }
    if storage is not None:
        record["payload"]["storage"] = storage
    if isinstance(report.get("public_candidate_descriptor"), dict) and \
            "appsbl.elf" in report["public_candidate_descriptor"].get("files", {}):
        headers = []
        for name in ("bfi_candidate_profile.generated.h", "bii_candidate_policy.generated.h"):
            path = provider_root / name
            headers.append({"path": provider_build_rel + "/" + name,
                            "sha256": sha(regular(path)), "bytes": path.stat().st_size})
        record["candidate_profile_headers"] = headers
    return record, report, provider_file, payload_file


def c_record(row):
    return {
        "record_id": row["record_id"], "candidate_id": row["candidate_id"],
        "hardware_class": row["hardware_class"],
        "source_sha256": row["source_sha256"], "product": row["product"],
        "route": row["route"], "route_compatible": row["route_compatible"],
        "source_revision_sha256": row["source_revision_sha256"],
        "stock_kernel_sha256": row["stock_kernel_sha256"],
        "bridge_kernel_sha256": row["bridge_kernel_sha256"],
        "nand_dt_sha256": row["nand_dt_sha256"],
        "profile_set_id": row["profile_set_id"],
        "candidate_set_id": row["candidate_set_id"],
        "nand_profiles": row["nand_profiles"],
        "provider_path": row["provider"]["path"],
        "provider_sha256": row["provider"]["sha256"],
        "provider_bytes": row["provider"]["bytes"],
        "payload_path": row["payload"]["path"],
        "payload_sha256": row["payload"].get("storage", row["payload"])["sha256"],
        "payload_bytes": row["payload"].get("storage", row["payload"])["bytes"],
    }


def run_production_dispatch_gate(identity, rows, expected_provider_rows, cc="cc"):
    """Require the compiled production dispatcher to select every final record."""
    from test_dispatch_e2e import run_dispatch_gate

    if (type(expected_provider_rows) is not int or not 1 <= expected_provider_rows <= 160 or
            len(rows) != expected_provider_rows):
        raise ValueError("final provider table differs from independently validated row count")
    records = [c_record(row) for row in rows]
    try:
        result = run_dispatch_gate(identity["sha256"], identity["model"], records,
                                   expected_provider_rows, cc=cc)
    except Exception as exc:
        raise ValueError("production dispatcher complete-record gate failed") from exc
    if (not isinstance(result, dict) or result.get("status") != "PASS" or
            result.get("source_sha256") != identity["sha256"] or
            result.get("product") != identity["model"] or
            result.get("provider_rows") != expected_provider_rows or
            result.get("tested_candidates") != expected_provider_rows or
            result.get("provider_executables_run") is not False):
        raise ValueError("production dispatcher complete-record gate returned an incomplete PASS")
    return result


def compile_dispatcher(cc, include_dir, output, log, return_exec_sha256=None, return_exec_bytes=0):
    source = build.PROJECT / "src/bridge_dispatch.c"
    return_exec = build.PROJECT / "src/bridge_oem_return_exec.c"
    if not source.is_file():
        raise ValueError("dispatcher sources missing")
    if not return_exec.is_file():
        raise ValueError("dispatcher return verifier source missing")
    if return_exec_sha256 is None:
        return_exec_sha256 = ""
    if (not isinstance(return_exec_sha256, str) or
            (return_exec_sha256 and (len(return_exec_sha256) != 64 or
             any(c not in "0123456789abcdef" for c in return_exec_sha256))) or
            type(return_exec_bytes) is not int or return_exec_bytes < 0 or
            bool(return_exec_sha256) != bool(return_exec_bytes)):
        raise ValueError("dispatcher return helper compile-time pin is malformed")
    compiler = Path(shutil.which(str(cc)) or str(cc)).resolve()
    command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static", "-s",
        "-fno-ident", "-Wl,--build-id=none", "-I", include_dir, "-I", build.PROJECT / "src",
        '-DBR_RETURN_EXEC_SHA256="%s"' % return_exec_sha256,
        "-DBR_RETURN_EXEC_BYTES=%du" % return_exec_bytes,
        source, return_exec, "-o", output]
    build.command(command, log)
    # The dispatcher verifies that executable artifacts have no write bits.
    output.chmod(0o555)
    try:
        version = subprocess.run([str(compiler), "--version"], check=True, capture_output=True,
                                 text=True, timeout=15).stdout.splitlines()[0]
    except (OSError, subprocess.SubprocessError, IndexError) as exc:
        raise ValueError("cannot identify dispatcher compiler") from exc
    source_files = {}
    for path in (source, return_exec, build.PROJECT / "src/bridge_dispatch.h",
                 build.PROJECT / "src/bridge_sha256.h"):
        source_files[path.name] = sha(regular(path))
    return {"compiler_path": str(compiler), "compiler_version": version,
            "command": [str(x) for x in command], "sources": source_files,
            "compile_log": str(log), "compile_log_sha256": sha(regular(log))}


def compile_source_return_helper(args, identity, official, provider_builds, rootfs, include_dir, out):
    """Compile a source-only return helper with its own NAND/OEM source contract."""
    base = provider_builds[0][0]
    root_binding = json.loads(regular(base / "BUILD-REPORT.json")).get("root_binding_contract")
    if not isinstance(root_binding, dict):
        raise ValueError("source-only return helper requires provider rootfs geometry contract")
    root_header_name = "bridge_root_binding_contract.generated.h"
    root_header_bytes = regular(base / root_header_name)
    if (root_binding.get("path") != root_header_name or
            root_binding.get("sha256") != sha(root_header_bytes) or
            root_binding.get("contract", {}).get("source_official_sha256") != identity["sha256"] or
            root_header_bytes != root_binding_contract.render_header(root_binding["contract"]).encode("ascii")):
        raise ValueError("source-only return helper rootfs contract differs from provider")
    for provider_out, _ in provider_builds[1:]:
        other = json.loads(regular(provider_out / "BUILD-REPORT.json")).get("root_binding_contract")
        if not isinstance(other, dict) or other.get("contract") != root_binding.get("contract"):
            raise ValueError("providers for one package have different rootfs geometry contracts")
    (include_dir / root_header_name).write_bytes(root_header_bytes)
    (include_dir / root_header_name).chmod(0o600)
    stock_ubi = base / "stock.ubi"
    stock_meta, stock_volumes, _, _ = vendor_ubi.parse(stock_ubi)
    if not isinstance(stock_volumes.get("kernel"), bytes):
        raise ValueError("exact source UBI lacks its stock kernel FIT")
    stock_fit = out / "stock-kernel.fit"
    bridge_fit = out / "bridge-kernel.fit"
    stock_fit.write_bytes(stock_volumes["kernel"])
    bridge_fit.write_bytes(regular(base / "kernel.fit"))
    stock_fit.chmod(0o600)
    bridge_fit.chmod(0o600)
    raw_catalog = nand_schema.profiles()
    catalog_bytes = nand_schema.CATALOG.read_bytes()
    if sha(catalog_bytes) != raw_catalog["catalog_sha256"]:
        raise ValueError("NAND catalog bytes differ from parsed source digest")
    catalog_header = include_dir / "h3c_nand_catalog.h"
    catalog_header.write_bytes(catalog_bytes)
    catalog_header.chmod(0o600)
    runtime = nand_schema.runtime_profiles(SOURCE_TABLE_VARIANTS[identity["sha256"]], 0,
                                           catalog=raw_catalog)
    geometries = sorted({(p["mtd_oob_bytes"], p["ecc_bits"]) for p in runtime})
    if not geometries:
        raise ValueError("source has no exact source/kernel NAND return geometries")
    slot_bytes = 40 * 1024 * 1024
    ubi = regular(stock_ubi)
    if len(ubi) > slot_bytes:
        raise ValueError("source UBI exceeds the fixed OEM return slot")
    padded_path = out / "return-source-slot.ubi"
    padded = ubi + b"\xff" * (slot_bytes - len(ubi))
    padded_path.write_bytes(padded)
    padded_path.chmod(0o600)
    # Reuse the existing narrowly reviewed autoresize normalizer. It validates
    # the exact source UBI table first; policy 2 only canonicalizes row 4's
    # autoresize fields while all other table rows and volume streams stay pinned.
    source_table_sha256 = build.public_autoresize_source_table(padded_path)
    manifest_rows = []
    for physical_offset in (0x900000, 0x3100000):
        for oob_bytes, ecc_bits in geometries:
            manifest_rows.append(build.oem_manifest.build_manifest(padded_path,
                expected_ubi_sha256=sha(padded), candidate_sha256=identity["sha256"],
                observed_geometry={"physical_offset": physical_offset, "size_bytes": slot_bytes,
                    "erase_bytes": 131072, "page_bytes": 2048,
                    "oob_bytes": oob_bytes, "ecc_bits": ecc_bits}))
    pool_entries = build.oem_pool_entries(manifest_rows, identity, exit_policy_override=1)
    pool_entries.extend(dict(entry, table_policy=2, alias=entry["alias"] + 10000)
                        for entry in tuple(pool_entries))
    pool = build.oem_manifest.c_pool_header(pool_entries)
    manifest_header = include_dir / "bridge_oem_return_manifest.h"
    manifest_header.write_text(pool, encoding="ascii")
    manifest_header.chmod(0o600)

    components = build.official_bootchain_components(official, identity)
    metadata_rows = []
    routes = sorted({build.PUBLIC_BOARD_INPUTS[board]["route"] for board in
                     source_boards(identity)})
    for route in routes:
        metadata_rows.append(build.return_source.metadata(regular(stock_fit), regular(bridge_fit),
            identity["sha256"], route=route))
    registry = build.return_source.c_return_contract_header(metadata_rows,
        {identity["sha256"]: components}, raw_catalog["profiles"],
        source_table_mask=sum(nand_schema.TABLE_MASKS[v] for v in SOURCE_TABLE_VARIANTS[identity["sha256"]]))
    registry_path = include_dir / "bridge_oem_return_registry.h"
    registry_path.write_text(registry, encoding="ascii")
    registry_path.chmod(0o600)

    output = rootfs / "usr/sbin/bridge-oem-return"
    output.parent.mkdir(parents=True, exist_ok=True)
    source_names = ("bridge_install.c", "bridge_version.c", "bridge_oem_slot.c",
        "bridge_backup.c", "bridge_root_binding.c", "bridge_first_install.c",
        "bridge_install_identity.c", "bridge_mtd_observer.c")
    source_paths = [build.PROJECT / "src" / name for name in source_names]
    source_paths.insert(0, build.PROJECT / "src/bridge_check.c")
    compiler = Path(shutil.which(str(args.cc)) or str(args.cc)).resolve()
    command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static", "-s",
        "-fno-ident", "-Wl,--build-id=none", '-DBRIDGE_RETURN_ONLY=1',
        '-DBI_HAVE_PAYLOAD=0', '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
        '-DBI_OEM_MANIFEST_HEADER="bridge_oem_return_manifest.h"',
        root_binding_contract.COMPILE_FLAG,
        f'-DBI_NAND_CATALOG_SHA256="{raw_catalog["catalog_sha256"]}"',
        "-I", include_dir, "-I", build.PROJECT / "src", *source_paths, "-o", output]
    build.command(command, out / "bridge-oem-return-compile.log")
    output.chmod(0o555)
    return {"path": "/usr/sbin/bridge-oem-return", "binary_path": "rootfs/usr/sbin/bridge-oem-return",
        "bytes": output.stat().st_size, "sha256": sha(output.read_bytes()), "mode": 0o555,
        "compile": {"command": [str(x) for x in command],
            "compile_log": str(out / "bridge-oem-return-compile.log"),
            "compile_log_sha256": sha(regular(out / "bridge-oem-return-compile.log"))},
        "registry_header_sha256": sha(registry.encode("ascii")),
        "manifest_header_sha256": sha(pool.encode("ascii")),
        "registry_header_path": "dispatcher-include/bridge_oem_return_registry.h",
        "manifest_header_path": "dispatcher-include/bridge_oem_return_manifest.h",
        "root_binding_contract_header_path": "dispatcher-include/bridge_root_binding_contract.generated.h",
        "root_binding_contract_header_sha256": sha(root_header_bytes),
        "root_binding_contract_sha256": root_binding_contract.contract_sha256(root_binding["contract"]),
        "catalog_header_path": "dispatcher-include/h3c_nand_catalog.h",
        "catalog_header_sha256": raw_catalog["catalog_sha256"],
        "source_slot_sha256": sha(padded), "source_volume_table_sha256": source_table_sha256,
        "profile_geometry_count": len(geometries), "pool_rule_count": len(pool_entries)}


def copy_provider_rootfs(base_rootfs, output_rootfs):
    """Make an isolated combined tree; child build reports bind the original tree."""
    base_rootfs = Path(base_rootfs)
    output_rootfs = Path(output_rootfs)
    if not base_rootfs.is_dir() or output_rootfs.exists() or output_rootfs.is_symlink():
        raise ValueError("provider rootfs source/destination is invalid")
    shutil.copytree(base_rootfs, output_rootfs, symlinks=True)
    return output_rootfs


def verify_bundle_ubi_volumes(new_volumes, stock_volumes, bridge_kernel):
    """Allow only the already-reviewed kernel transform and combined rootfs delta."""
    for name in ("wifi_fw", "bt_fw"):
        if new_volumes.get(name) != stock_volumes.get(name):
            raise ValueError("non-rootfs immutable volume changed: " + name)
    expected_kernel = regular(bridge_kernel)
    if new_volumes.get("kernel") != expected_kernel:
        raise ValueError("combined kernel volume differs from reviewed bridge FIT")


def install_bundle_file(source, target, mode):
    """Copy a new file, or validate and retain an identical immutable base file."""
    source = Path(source)
    target = Path(target)
    source_stat = source.lstat()
    if not source.is_file() or source.is_symlink() or not stat.S_ISREG(source_stat.st_mode):
        raise ValueError("bundle source must be a regular non-symlink file")
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() or target.is_symlink():
        target_stat = target.lstat()
        if (target.is_symlink() or not stat.S_ISREG(target_stat.st_mode) or
                target.read_bytes() != source.read_bytes() or
                (target_stat.st_mode & 0o777) != mode):
            raise ValueError("existing bundle file differs in type, bytes, or mode")
        return
    shutil.copyfile(source, target)
    target.chmod(mode)


def ubi_fit_node_name(ubi):
    return "ubi-bridge-" + hashlib.sha1(regular(ubi)).hexdigest()


def repack(args, official, identity, records, provider_builds):
    out = args.output
    base = provider_builds[0][0]
    common_root_contract = records[0][0]["root_binding_contract"]
    if any(row[0]["root_binding_contract"] != common_root_contract for row in records[1:]):
        raise ValueError("provider records do not share one source-bound rootfs geometry contract")
    rootfs = out / "rootfs"
    copy_provider_rootfs(base / "rootfs", rootfs)
    for row, _, provider_file, payload_file in records:
        for source, key in ((provider_file, "provider"), (payload_file, "payload")):
            target = rootfs / row[key]["path"].lstrip("/")
            install_bundle_file(source, target, row[key]["mode"])
    include = out / "dispatcher-include"
    include.mkdir(mode=0o700)
    header_text = build.generated_provider_bundle_header(identity["sha256"], identity["model"],
        [c_record(row) for row, *_ in records])
    header_path = include / "bridge_provider_bundle.h"
    header_path.write_text(header_text)
    header_path.chmod(0o600)
    return_helper = compile_source_return_helper(args, identity, official, provider_builds,
        rootfs, include, out)
    dispatcher = rootfs / "usr/sbin/bridge-check"
    dispatcher_compile = compile_dispatcher(args.cc, include, dispatcher, out / "dispatcher-compile.log",
        return_helper["sha256"], return_helper["bytes"])
    dispatcher_sha = sha(dispatcher.read_bytes())

    stock_ubi = base / "stock.ubi"
    ubi_meta, stock_volumes, _, _ = vendor_ubi.parse(stock_ubi)
    table = ubi_meta
    squash = out / "rootfs.squashfs"
    build.command([args.mksquashfs, rootfs, squash, "-comp", "xz", "-b", "131072",
        "-noappend", "-all-root", "-no-xattrs", "-no-progress", "-processors", "1",
        "-mkfs-time", str(build.EPOCH), "-all-time", str(build.EPOCH),
        "-pf", base / "device-nodes.pseudo"], out / "mksquashfs-bundle.log")
    images = {"kernel": base / "kernel.fit", "wifi_fw": base / "wifi_fw.bin",
              "bt_fw": base / "bt_fw.bin", "ubi_rootfs": squash}
    config = []
    for vol in table["volume_table"]:
        lines = ["[" + vol["name"] + "]", "mode=ubi", "vol_id=" + str(vol["id"]),
                 "vol_name=" + vol["name"], "vol_type=" + vol["type"],
                 "vol_size=" + str(vol["capacity"])]
        if vol["name"] in images:
            if images[vol["name"]].stat().st_size > vol["capacity"]:
                raise ValueError("combined rootfs exceeds OEM volume capacity")
            lines.append("image=" + str(images[vol["name"]]))
        if vol["autoresize"]:
            lines.append("vol_flags=autoresize")
        config.extend(lines + [""])
    config_path = out / "ubinize.cfg"
    config_path.write_text("\n".join(config))
    bridge_ubi = out / "bridge.ubi"
    build.command([args.ubinize, "-o", bridge_ubi, "-m", "2048", "-p", "131072",
        "-s", "2048", "-O", "2048", "-Q", str(build.EPOCH), config_path], out / "ubinize.log")
    new_meta, new_volumes, _, _ = vendor_ubi.parse(bridge_ubi)
    built_rootfs_geometry = root_binding_contract.verify_same_source_and_built(
        common_root_contract, regular(bridge_ubi))
    if new_meta["volume_table"] != table["volume_table"]:
        raise ValueError("combined UBI volume table differs from OEM source")
    verify_bundle_ubi_volumes(new_volumes, stock_volumes, base / "kernel.fit")
    rootfs_bytes = regular(squash)
    rootfs_volume = new_volumes["ubi_rootfs"]
    if rootfs_volume[:len(rootfs_bytes)] != rootfs_bytes or rootfs_volume[len(rootfs_bytes):] != \
       b"\xff" * (len(rootfs_volume) - len(rootfs_bytes)):
        raise ValueError("combined UBI rootfs padding differs")

    node = ubi_fit_node_name(bridge_ubi)
    its = ('/dts-v1/; / { description = "BootBridge source-bound provider bundle"; '
        '#address-cells = <1>; timestamp = <1791907200>; images { %s { '
        'description = "BootBridge provider bundle UBI"; data = /incbin/("bridge.ubi"); '
        'type = "firmware"; arch = "arm"; compression = "none"; '
        'hash@1 { algo = "crc32"; value = [00 00 00 00]; }; '
        'hash@2 { algo = "sha1"; value = [%s]; }; }; }; '
        'configurations { default = "conf-bridge"; conf-bridge { firmware = "%s"; }; }; };') % (
            node, "00 " * 20, node)
    its_path = out / "bridge.its"
    its_path.write_text(its)
    build.command([args.dtc, "-I", "dts", "-O", "dtb", "-o",
        out / "bridge-unhashed.itb", its_path], out / "dtc.log")
    fit = build.FDT(regular(out / "bridge-unhashed.itb"))
    for name, algo in (("hash@1", "crc32"), ("hash@2", "sha1")):
        fit.set("/images/" + node + "/" + name, "value", build.digest(regular(bridge_ubi), algo))
    bridge_fit = fit.bytes()
    (out / "bridge.itb").write_bytes(bridge_fit)
    bridge_kernel_path = out / "bridge-kernel.fit"
    bridge_kernel_path.write_bytes(regular(base / "kernel.fit"))
    stock_kernel_path = out / "stock-kernel.fit"
    stock_kernel_path.write_bytes(stock_volumes["kernel"])

    outer_payload = bridge_fit + b"\0" * (-len(bridge_fit) % 4)
    header = bytearray(official[:160])
    struct.pack_into(">I", header, 68, 96 + len(outer_payload))
    struct.pack_into(">I", header, 80, len(outer_payload))
    struct.pack_into(">I", header, 72, 0)
    packaged = header + outer_payload
    checksum = sum(v[0] for v in struct.iter_unpack(">I", packaged[64:])) & 0xffffffff
    struct.pack_into(">I", packaged, 72, (0xffffffff - checksum) & 0xffffffff)
    if (sum(v[0] for v in struct.iter_unpack(">I", packaged[64:])) & 0xffffffff != 0xffffffff or
            packaged[99:160] != official[99:160]):
        raise ValueError("outer checksum or OEM gate preservation failed")
    candidate = out / (identity["identity"] + "-BOOTBRIDGE-BUNDLE-TRIAL.bin")
    candidate.write_bytes(packaged)
    candidate.chmod(0o600)
    if not getattr(args, "root_binding_gate_config", None):
        raise ValueError("production bundle build requires --root-binding-gate-config")
    root_gate = root_binding_contract.run_live_gate(
        args.root_binding_gate_config, identity["model"], common_root_contract,
        include / "bridge_root_binding_contract.generated.h", bridge_ubi,
        out / "root-binding-live-gate", relative_to=out)
    rows = [r[0] for r in records]
    first_report = records[0][1]
    provenance = records[0][1]["rootfs"].get("oem_input_files", {})
    generated_paths = {r[0]["provider"]["path"].lstrip("/") for r in records}
    generated_paths |= {r[0]["payload"]["path"].lstrip("/") for r in records}
    generated_paths.add("usr/sbin/bridge-check")
    file_inventory = []
    for path in sorted(rootfs.rglob("*")):
        rel = path.relative_to(rootfs).as_posix()
        st = path.lstat()
        if path.is_symlink():
            entry = {"path": rel, "type": "symlink", "mode": st.st_mode & 0o777,
                     "target": os.readlink(path)}
        elif path.is_dir():
            entry = {"path": rel, "type": "directory", "mode": st.st_mode & 0o777}
        elif path.is_file():
            data = path.read_bytes()
            entry = {"path": rel, "type": "regular", "mode": st.st_mode & 0o777,
                     "bytes": len(data), "sha256": sha(data)}
        else:
            raise ValueError("unsupported inode in bundled rootfs: " + rel)
        if rel in provenance:
            entry["origin"] = "official-curated"
            entry["official_source_record"] = provenance[rel]
        elif rel in generated_paths:
            entry["origin"] = ("compiled-dispatcher" if rel == "usr/sbin/bridge-check" else
                               "compiled-provider" if rel in {r[0]["provider"]["path"].lstrip("/") for r in records}
                               else "public-candidate")
        else:
            entry["origin"] = "bridge-runtime-generated"
        file_inventory.append(entry)
    profile_set_doc = source_nand_profile_set(identity)
    report = {
        "schema": "bootbridge-provider-bundle-v2", "status": "OFFLINE_LAB_CANDIDATE",
        "device_operations": False, "hardware_validation": "NOT_EXECUTED",
        "install_available": True, "physical_install_qualification": "NOT_GRANTED",
        "official": {**{k: identity[k] for k in ("identity", "model", "sha256", "bytes")},
            "version": official_version(identity)},
        "source_identity": {"header_product": identity["model"],
            "version": official_version(identity), "sha256": identity["sha256"],
            "bytes": identity["bytes"]},
        "supported_product_identities": supported_product_identities(identity["sha256"]),
        "official_path": str(args.official.resolve()),
        "candidate_archive_path": str(args.candidate_archive.resolve()),
        "candidate_archive_sha256": getattr(args, "candidate_archive_sha256", None) or candidate_manifest.ARCHIVE_SHA256,
        "candidate_manifest_sha256": getattr(args, "candidate_manifest_sha256", None) or candidate_manifest.MANIFEST_SHA256,
        "candidate_descriptors_sha256": getattr(args, "candidate_descriptors_sha256", None) or candidate_manifest.CANDIDATES_SHA256,
        "candidate_archive_bytes": getattr(args, "candidate_archive_bytes", args.candidate_archive.stat().st_size),
        "candidate_manifest_bytes": getattr(args, "candidate_manifest_bytes", None),
        "candidate_descriptors_bytes": getattr(args, "candidate_descriptors_bytes", None),
        "candidate_set_id": records[0][0]["candidate_set_id"],
        "candidate_archive_candidate_count": getattr(args, "candidate_archive_candidate_count", len(rows)),
        "source_ineligible_candidates": getattr(args, "source_ineligible_candidates", []),
        "nand_catalog_sha256": nand_schema.profiles()["catalog_sha256"],
        "profile_sets": {profile_set_doc["profile_set_id"]: profile_set_doc},
        "candidate_inventory": getattr(args, "candidate_inventory", []),
        "candidate_path": candidate.name,
        "candidate": {"path": candidate.name, "bytes": len(packaged), "sha256": sha(packaged)},
        "providers": rows,
        "dispatcher": {"path": "/usr/sbin/bridge-check", "sha256": dispatcher_sha,
            "bytes": dispatcher.stat().st_size, "mode": 0o555,
            "binary_path": "rootfs/usr/sbin/bridge-check",
            "header_path": "dispatcher-include/bridge_provider_bundle.h",
            "header_sha256": sha(regular(header_path)), "records_count": len(rows),
            "production_gate": args.production_dispatch_gate,
            "compile": dispatcher_compile},
        "return_helper": return_helper,
        "root_binding_contract": {
            **root_binding_contract.report_record(common_root_contract,
                "dispatcher-include/bridge_root_binding_contract.generated.h"),
            "built_ubi": built_rootfs_geometry,
        },
        "root_binding_gate": root_gate,
        "kernel": {"stock_fit_path": "stock-kernel.fit",
            "bridge_fit_path": "bridge-kernel.fit",
            "stock_fit_sha256": sha(stock_volumes["kernel"]),
            "bridge_fit_sha256": sha(regular(base / "kernel.fit")),
            "compressed_kernel_sha256": first_report["return_source"]["compressed_kernel_sha256"],
            "default_preserved": True, "all_configs_preserved": True,
            "routes": [{"route": r["route"], "compatible": r["route_compatible"],
                        "nand_dt_sha256": r["nand_dt_sha256"]} for r in rows]},
        "return_policy": "SOURCE_BOUND_TRIAL_RETURN_HELPER; NOT_INSTALL_READY",
        "rootfs": {"squashfs_path": "rootfs.squashfs", "tree_path": "rootfs",
            "bytes": len(rootfs_bytes), "sha256": sha(rootfs_bytes),
            "base_source_report": records[0][0]["provider_report"],
            "pseudo_path": (provider_builds[0][0].relative_to(out) / "device-nodes.pseudo").as_posix(),
            "pseudo_sha256": sha(regular(provider_builds[0][0] / "device-nodes.pseudo")),
            "file_inventory": file_inventory},
        "ubi_volume_table_sha256": first_report["ubi"]["volume_table_raw_sha256"],
        "not_executed": ["target device hardware matching", "OEM upload",
                         "runtime dispatcher on hardware", "provider installation on hardware"],
    }
    (out / "BUILD-REPORT.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    (out / "BUILD-REPORT.json").chmod(0o600)
    return report


def build_bundle(args):
    if os.environ.get("PYTHONOPTIMIZE"):
        raise ValueError("PYTHONOPTIMIZE is forbidden")
    official = regular(args.official)
    identity = build.identify_official(official)
    boards = source_boards(identity)
    fresh_pins = (getattr(args, "candidate_archive_sha256", None),
                  getattr(args, "candidate_manifest_sha256", None),
                  getattr(args, "candidate_descriptors_sha256", None))
    fresh = any(value is not None for value in fresh_pins)
    if fresh:
        if not all(fresh_pins):
            raise ValueError("fresh candidate archive requires all three independent SHA-256 pins")
        parsed_archive = candidate_manifest.read_candidate_archive_v2(args.candidate_archive, *fresh_pins)
        args.candidate_archive_bytes = parsed_archive["archive_bytes"]
        args.candidate_manifest_bytes = parsed_archive["manifest_bytes"]
        args.candidate_descriptors_bytes = parsed_archive["candidates_bytes"]
        descriptors = parsed_archive["candidates"]
        candidate_rows = []
        for descriptor in descriptors:
            files = descriptor["files"]
            candidate_rows.append({"candidate_id": descriptor["candidate_id"],
                "board": descriptor["board"], "nand_profile_id": descriptor["nand_profile_id"],
                "load_ecc_bits": descriptor["load_ecc_bits"],
                "storage_ecc_bits": descriptor["storage_ecc_bits"],
                "partition_sha256": files["appsbl.partition.bin"]["sha256"],
                "mbn_sha256": files["appsbl.mbn"]["sha256"],
                "load_sha256": descriptor["container"]["load"]["sha256"],
                "partition_bytes": files["appsbl.partition.bin"]["bytes"]})
        candidate_set = nand_schema.candidate_set_id(identity["sha256"], fresh_pins[0], candidate_rows)
        args.candidate_inventory = candidate_rows
        source_variants = SOURCE_TABLE_VARIANTS[identity["sha256"]]
        _, source_profile_ids = nand_schema.profile_set_id(identity["sha256"], source_variants)
        catalog_rows = {p["profile_id"]: p for p in nand_schema.profiles()["profiles"]}
        table_mask = 0
        for variant in source_variants:
            table_mask |= nand_schema.TABLE_MASKS[variant]
        seen_formats = set()
        for desc in descriptors:
            profile = catalog_rows.get(desc["nand_profile_id"])
            key = (desc["board"], desc["nand_profile_id"], desc["load_ecc_bits"], desc["storage_ecc_bits"])
            if desc["load_ecc_bits"] != desc["storage_ecc_bits"]:
                raise ValueError("fresh candidate LOAD ECC must equal runtime storage ECC")
            if key in seen_formats:
                raise ValueError("candidate archive repeats a board/profile/load/storage format")
            seen_formats.add(key)
            if profile is None or desc["board"] not in boards:
                raise ValueError("candidate archive has unknown profile or source board")
        expected_archive_formats = candidate_archive_format_set(catalog_rows, boards)
        if seen_formats != expected_archive_formats:
            missing = sorted(expected_archive_formats - seen_formats)
            unexpected = sorted(seen_formats - expected_archive_formats)
            raise ValueError("fresh candidate archive differs from full U-Boot/QPIC NAND matrix: "
                f"expected={len(expected_archive_formats)} actual={len(seen_formats)} "
                f"missing={missing[:8]} unexpected={unexpected[:8]}")
        expected_formats = set()
        for profile_id in source_profile_ids:
            profile = catalog_rows[profile_id]
            formats = [ecc for ecc, bit, field in ((4,1,"appsbl_ecc4_source_mask"),(8,2,"appsbl_ecc8_source_mask"))
                       if profile["appsbl_ecc_mode_mask"] & bit and profile[field] & table_mask and
                       profile["runtime_ecc_mode_mask"] & bit and profile["kernel_ecc_mode_mask"] & bit]
            for board in boards:
                for ecc in formats:
                    expected_formats.add((board, profile_id, ecc, ecc))
        actual_source_formats = {key for key in seen_formats if key in expected_formats}
        if actual_source_formats != expected_formats:
            raise ValueError("fresh candidate archive does not exactly cover source-derived board/profile/load/storage formats")
        args.candidate_archive_candidate_count = len(parsed_archive["candidates"])
        args.source_ineligible_candidates = [
            {"candidate_id": d["candidate_id"], "board": d["board"],
             "nand_profile_id": d["nand_profile_id"],
             "reason": "NO_EXACT_SOURCE_KERNEL_PROFILE"}
            for d in parsed_archive["candidates"]
            if (d["board"], d["nand_profile_id"], d["load_ecc_bits"], d["storage_ecc_bits"])
                not in expected_formats]
        descriptors = [d for d in descriptors
            if (d["board"], d["nand_profile_id"], d["load_ecc_bits"], d["storage_ecc_bits"])
                in expected_formats]
        if len(descriptors) > 160:
            raise ValueError("candidate archive exceeds ABI v2 provider row limit")
        for board in boards:
            covered = {d["nand_profile_id"] for d in descriptors if d["board"] == board}
            if covered != set(source_profile_ids):
                missing = sorted(set(source_profile_ids) - covered)
                extra = sorted(covered - set(source_profile_ids))
                raise ValueError("candidate archive does not exactly cover source-compatible NAND profiles for HW-%s (missing=%s extra=%s)" %
                                 (board, ",".join(missing), ",".join(extra)))
    else:
        # Frozen baseline archive keeps its immutable historical pins.
        parsed_archive = candidate_manifest.read_public_candidates(args.candidate_archive,
            candidate_manifest.ARCHIVE_SHA256)
        args.candidate_archive_bytes = parsed_archive["archive_bytes"]
        args.candidate_manifest_bytes = parsed_archive["manifest_bytes"]
        args.candidate_descriptors_bytes = parsed_archive["candidates_bytes"]
        baseline_rows = []
        for board, desc in parsed_archive["boards"].items():
            files = desc["files"]
            baseline_rows.append({"candidate_id": desc["candidate_id"], "board": board,
                "nand_profile_id": build.PUBLIC_BOARD_INPUTS[board]["nand_profile_id"],
                "load_ecc_bits": build.PUBLIC_BOARD_INPUTS[board]["load_ecc"],
                "storage_ecc_bits": build.PUBLIC_BOARD_INPUTS[board]["ecc"],
                "partition_sha256": files["appsbl.partition.bin"]["sha256"],
                "mbn_sha256": files["appsbl.mbn"]["sha256"],
                "load_sha256": desc["container"]["load"]["sha256"],
                "partition_bytes": files["appsbl.partition.bin"]["bytes"]})
        candidate_set = nand_schema.candidate_set_id(identity["sha256"],
            candidate_manifest.ARCHIVE_SHA256, baseline_rows)
        args.candidate_inventory = baseline_rows
        descriptors = None
    if args.output.exists():
        raise ValueError("new bundle output directory required; preserve existing evidence")
    args.output.mkdir(mode=0o700, parents=True)
    providers_root = args.output / "provider-builds"
    providers_root.mkdir(mode=0o700)
    provider_builds = []
    records = []
    if fresh:
        for index, descriptor in enumerate(descriptors):
            board = descriptor["board"]
            if board not in boards:
                raise ValueError("candidate board is outside the exact source board matrix")
            slug = "c-" + hashlib.sha256(descriptor["candidate_id"].encode("utf-8")).hexdigest()[:20]
            provider_out = providers_root / ("VARIANT-%03d" % index)
            namespace = SimpleNamespace(official=args.official, rootfs=args.rootfs,
                output=provider_out, cc=args.cc, ubinize=args.ubinize, uboot=None,
                payload_report=args.payload_report, candidate_archive=args.candidate_archive,
                candidate_descriptor=descriptor,
                source_table_variants=SOURCE_TABLE_VARIANTS[identity["sha256"]],
                hardware_class=board, mksquashfs=args.mksquashfs, dtc=args.dtc,
                provider_install_path=f"/usr/sbin/bridge-candidates/{slug}/provider",
                payload_install_path=f"/etc/bridge/providers/{slug}/uboot.bin")
            build.build(namespace)
            provider_builds.append((provider_out, board))
            records.append(provider_material(board, identity, provider_out, candidate_set))
    else:
        for board in boards:
            provider_out = providers_root / ("HW-" + board)
            namespace = SimpleNamespace(official=args.official, rootfs=args.rootfs,
                output=provider_out, cc=args.cc, ubinize=args.ubinize, uboot=None,
                payload_report=args.payload_report, candidate_archive=args.candidate_archive,
                hardware_class=board, mksquashfs=args.mksquashfs, dtc=args.dtc,
                provider_install_path=PROVIDER_PATH.format(board=board),
                payload_install_path=PAYLOAD_PATH.format(board=board))
            build.build(namespace)
            provider_builds.append((provider_out, board))
            records.append(provider_material(board, identity, provider_out, candidate_set))
    final_rows = [record[0] for record in records]
    expected_provider_rows = len(descriptors) if fresh else len(boards)
    args.production_dispatch_gate = run_production_dispatch_gate(
        identity, final_rows, expected_provider_rows, cc="cc")
    reference_fit = regular(provider_builds[0][0] / "kernel.fit")
    for provider_out, _ in provider_builds[1:]:
        if regular(provider_out / "kernel.fit") != reference_fit:
            raise ValueError("providers for one OEM source have different transformed FIT bytes")
    report = repack(args, official, identity, records, provider_builds)
    print(json.dumps({"status": report["status"], "official": report["official"],
        "candidate": report["candidate"],
        "provider_classes": [row[0]["hardware_class"] for row in records]}, ensure_ascii=False))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("official", "rootfs", "output", "cc", "ubinize",
                "candidate-archive", "payload-report"):
        parser.add_argument("--" + key, required=True, type=Path)
    parser.add_argument("--mksquashfs", default="mksquashfs")
    parser.add_argument("--dtc", default="dtc")
    parser.add_argument("--candidate-archive-sha256")
    parser.add_argument("--candidate-manifest-sha256")
    parser.add_argument("--candidate-descriptors-sha256")
    parser.add_argument("--root-binding-gate-config", required=True, type=Path,
                        help="pinned inputs for the required live QEMU GlueBI/sysfs gate")
    build_bundle(parser.parse_args())


if __name__ == "__main__":
    main()


