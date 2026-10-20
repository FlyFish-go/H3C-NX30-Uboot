#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Independent offline audit for a source-bound multi-provider U-Boot bundle.

This deliberately recomputes the OEM wrapper, nested UBI, FIT routes, candidate
payloads, profile closure, dispatcher records and file hashes. A builder's PASS
flag is never treated as evidence. No device, network, or Windows executable is
used by this module.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile

import audit
import build
import build_bundle as bundle_build
import bridge_kernel
import build_return_source
import candidate_manifest
import nand_schema
import vendor_fit
import vendor_ubi
import root_binding_contract


# Exact official-source pins, separate from the 17-target product matrix.
# This NX30-header R012 byte stream has independent RX3000 provenance too;
# those logical identities remain distinct and map to their own class sets.
SUPPORTED_SOURCE_IDENTITIES = {
    "69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44": (
        ("RC3000", ("A", "B", "C", "D")),),
    "cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b": (
        ("RT3000", ("A", "B", "C", "D")),),
    "5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5": (
        ("RW3000", ("A", "B", "C", "D")),),
    "61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922": (
        ("NX30", ("D",)), ("RX3000", ("A", "B", "C", "D"))),
}
SUPPORTED_SOURCE_CLASSES = {
    source: tuple(sorted({board for _, boards in identities for board in boards}))
    for source, identities in SUPPORTED_SOURCE_IDENTITIES.items()
}
SOURCE_IDENTITY_EVIDENCE = {
    ("61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "RX3000"):
        {"path": "source/nx30-oem-bridge/tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json",
         "bytes": 13544,
         "sha256": "9834e1b2b92b59d4822905fc5ebe9096462e44106534b440598c85a67c2bf4c6"},
}
# Bundle providers run as fixed helper binaries and are intentionally not
# writable by the service account.  The builder's previous 0755 staging mode
# was too permissive for this contract.
PROVIDER_MODES = (0o555,)
PAYLOAD_MODE = 0o400
DISPATCHER_MODES = (0o555,)
MAX_PROVIDER_BYTES = 32 * 1024 * 1024

PROVIDER_STAGE_SOURCES = (
    "bridge_check.c", "bridge_install.c", "bridge_version.c", "bridge_oem_slot.c",
    "bridge_backup.c", "bridge_mtd_observer.c", "bridge_first_install.c",
    "bridge_root_binding.c", "bridge_install_identity.c",
)


def _provider_stage_command(compiler, include_dir, project_src, output):
    """Recreate the production stage_rootfs provider command in source order."""
    source_paths = [Path(project_src) / name for name in PROVIDER_STAGE_SOURCES]
    return [str(compiler), "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static", "-s",
        "-fno-ident", "-Wl,--build-id=none",
        '-DBI_OEM_MANIFEST_HEADER="bridge_oem_manifest.h"',
        '-DBI_RETURN_SOURCE_HEADER="bridge_oem_return_source.h"',
        '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
        "-DBI_RETURN_SOURCE_REGISTRY_V2=1",
        '-DBI_FIRST_INSTALL_PROFILE_HEADER="bridge_first_install_profile.h"',
        '-DBI_PAYLOAD_HEADER="bridge_payload.h"',
        '-DBFI_PROFILE_RECORD_HEADER="bfi_candidate_profile.generated.h"',
        '-DBII_CANDIDATE_POLICY_HEADER="bii_candidate_policy.generated.h"',
        '-DBI_PAYLOAD_PATCH_HEADER="bridge_payload_patch.h"',
        root_binding_contract.COMPILE_FLAG,
        "-I", str(include_dir), "-I", str(project_src), *map(str, source_paths),
        "-o", str(output)]


class BundleAuditError(ValueError):
    """Bundle or evidence does not satisfy the independently checked contract."""


def archive_candidate_format_matrix(catalog_rows, boards):
    """Independent archive matrix retaining formats unsupported by Linux."""
    all_table_mask = sum(nand_schema.TABLE_MASKS.values())
    expected = set()
    for profile in catalog_rows.values():
        choices = []
        for ecc, bit, source_key in ((4, 1, "appsbl_ecc4_source_mask"),
                                     (8, 2, "appsbl_ecc8_source_mask")):
            if (profile["appsbl_ecc_mode_mask"] & bit and
                    profile["runtime_ecc_mode_mask"] & bit and profile[source_key] & all_table_mask):
                choices.append(ecc)
        for board in boards:
            for ecc in choices:
                expected.add((board, profile["profile_id"], ecc, ecc))
    return expected


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _hex_digest(value, label):
    if (not isinstance(value, str) or len(value) != 64 or
            any(c not in "0123456789abcdef" for c in value)):
        raise BundleAuditError(label + " must be a lowercase SHA-256")
    return value


def _safe_relpath(value, label):
    if not isinstance(value, str) or not value or "\\" in value:
        raise BundleAuditError(label + " must be a relative POSIX path")
    p = PurePosixPath(value)
    if p.is_absolute() or any(part in ("", ".", "..") for part in value.split("/")):
        raise BundleAuditError(label + " is not a safe relative path")
    return p


def _file(root: Path, value, label):
    rel = _safe_relpath(value, label)
    path = root.joinpath(*rel.parts)
    try:
        st = path.lstat()
    except OSError as exc:
        raise BundleAuditError(label + " is missing") from exc
    if not stat.S_ISREG(st.st_mode) or path.is_symlink():
        raise BundleAuditError(label + " must be an ordinary non-symlink file")
    return path, path.read_bytes(), st


def _mode(st):
    return stat.S_IMODE(st.st_mode)


def _same_executable(command_value, compiler_value):
    """Accept a bare name or symlink only when it resolves to the pinned compiler."""
    if not isinstance(command_value, str) or not command_value or "\x00" in command_value:
        return False
    if not isinstance(compiler_value, str) or not compiler_value or "\x00" in compiler_value:
        return False
    try:
        pinned = Path(compiler_value).resolve(strict=True)
        recorded = Path(command_value)
        if not recorded.is_absolute():
            located = shutil.which(command_value)
            if located is None:
                return False
            recorded = Path(located)
        recorded = recorded.resolve(strict=True)
        return pinned.is_file() and recorded.is_file() and os.path.samefile(pinned, recorded)
    except (OSError, RuntimeError):
        return False


def _required(obj, key, kind, label):
    value = obj.get(key) if isinstance(obj, dict) else None
    if not isinstance(value, kind):
        raise BundleAuditError(label + "." + key + " has the wrong type")
    return value


def _json_unique(raw, label):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise BundleAuditError(label + " has a duplicate JSON key")
            result[key] = value
        return result
    try:
        return json.loads(raw, object_pairs_hook=unique)
    except (UnicodeError, json.JSONDecodeError) as exc:
        raise BundleAuditError(label + " is invalid JSON") from exc


def _exact_record_keys(row, expected, label):
    if not isinstance(row, dict) or set(row) != set(expected):
        raise BundleAuditError(label + " fields do not match the bundle ABI")


def _validate_source_identity(report, identity, workspace_root):
    """Bind product/version declarations to the already verified official bytes."""
    token = identity.get("identity")
    model = identity.get("model")
    if (not isinstance(token, str) or not isinstance(model, str) or
            not token.startswith(model + "V") or not token.isascii()):
        raise BundleAuditError("exact official source has no canonical product/version token")
    official = report.get("official")
    if not isinstance(official, dict) or official.get("version") != token:
        raise BundleAuditError("reported official version differs from its validated header token")
    source_identity = report.get("source_identity")
    expected = {"header_product": model, "version": token,
                "sha256": identity["sha256"], "bytes": identity["bytes"]}
    if source_identity != expected:
        raise BundleAuditError("bundle source identity differs from the exact official input")
    supported = report.get("supported_product_identities")
    allowed = SUPPORTED_SOURCE_IDENTITIES.get(identity["sha256"])
    if allowed is None or (model, tuple(sorted(dict(allowed).get(model, ())))) not in allowed:
        raise BundleAuditError("official header product has no exact source identity approval")
    expected_supported = [
        {"product": product, "source_sha256": identity["sha256"],
         "hardware_classes": list(classes)}
        for product, classes in allowed
    ]
    for row, (product, _) in zip(expected_supported, allowed):
        evidence = SOURCE_IDENTITY_EVIDENCE.get((identity["sha256"], product))
        if evidence is not None:
            row["identity_evidence"] = dict(evidence)
    if supported != expected_supported:
        raise BundleAuditError("supported product identities differ from exact source approval")
    for row in expected_supported:
        evidence = row.get("identity_evidence")
        if evidence is None:
            continue
        evidence_path, evidence_bytes, _ = _file(workspace_root, evidence["path"],
                                                  "supported_product_identities.identity_evidence")
        if len(evidence_bytes) != evidence["bytes"] or sha256(evidence_bytes) != evidence["sha256"]:
            raise BundleAuditError("external product identity evidence pin mismatch")


def _compare_profile_row(row, expected, board):
    """Compare the report row with route facts recomputed from the actual FIT."""
    checks = {
        "source_sha256": expected["official_sha256"],
        "route": expected["route"],
        "route_compatible": expected["compatible"],
        "stock_kernel_sha256": expected["stock_kernel_sha256"],
        "bridge_kernel_sha256": expected["bridge_kernel_sha256"],
        "nand_dt_sha256": expected["nand_dt_sha256"],
    }
    for key, value in checks.items():
        if row.get(key) != value:
            raise BundleAuditError("HW-" + board + " route metadata mismatch: " + key)
    if row.get("return_source") != expected:
        raise BundleAuditError("HW-" + board + " full return-source metadata differs from recomputed FIT facts")


def _source_revision_binding(descriptor, board):
    """Recompute the candidate/archive binding used by the provider C ABI."""
    if not isinstance(descriptor, dict) or not isinstance(descriptor.get("files"), dict):
        raise BundleAuditError("candidate source-revision descriptor is malformed")
    mbn = descriptor["files"].get("appsbl.mbn")
    partition = descriptor["files"].get("appsbl.partition.bin")
    if not isinstance(mbn, dict) or not isinstance(partition, dict):
        raise BundleAuditError("candidate source-revision payload descriptor is missing")
    binding = {
        "schema": "bootbridge-candidate-source-revision-v1",
        "archive_sha256": candidate_manifest.ARCHIVE_SHA256,
        "manifest_sha256": candidate_manifest.MANIFEST_SHA256,
        "candidates_sha256": candidate_manifest.CANDIDATES_SHA256,
        "board": descriptor.get("board"),
        "candidate_id": descriptor.get("candidate_id"),
        "source_revision_git": descriptor.get("source_revision"),
        "appsbl_mbn_sha256": mbn.get("sha256"),
        "appsbl_partition_sha256": partition.get("sha256"),
    }
    if binding["board"] != board:
        raise BundleAuditError("candidate source-revision board mismatch")
    if (not isinstance(binding["candidate_id"], str) or not binding["candidate_id"] or
            not isinstance(binding["source_revision_git"], str) or
            len(binding["source_revision_git"]) != 40 or
            any(c not in "0123456789abcdef" for c in binding["source_revision_git"])):
        raise BundleAuditError("candidate source-revision identity is malformed")
    for key in ("archive_sha256", "manifest_sha256", "candidates_sha256",
                "appsbl_mbn_sha256", "appsbl_partition_sha256"):
        _hex_digest(binding[key], "source_revision_binding." + key)
    canonical = json.dumps(binding, sort_keys=True, separators=(",", ":"),
                           ensure_ascii=True).encode("ascii")
    return binding, sha256(canonical)


def _extract_bundle(report, bundle_root, archive_path, workspace_root):
    if report.get("schema") != "bootbridge-provider-bundle-v1":
        raise BundleAuditError("unsupported bundle report schema")
    if report.get("status") != "OFFLINE_LAB_BUNDLE":
        raise BundleAuditError("bundle is not marked as an offline lab artifact")
    if report.get("device_operations") is not False or report.get("hardware_validation") != "NOT_EXECUTED":
        raise BundleAuditError("bundle report makes a device-operation or hardware-validation claim")
    candidate_name = _required(report, "candidate_path", str, "report")
    candidate_path, candidate, candidate_st = _file(bundle_root, candidate_name, "candidate_path")
    candidate_meta = _required(report, "candidate", dict, "report")
    if (candidate_meta.get("path") != candidate_name or candidate_meta.get("bytes") != len(candidate) or
            _hex_digest(candidate_meta.get("sha256"), "candidate.sha256") != sha256(candidate)):
        raise BundleAuditError("candidate file differs from its report pin")
    official = _required(report, "official", dict, "report")
    official_name = _required(report, "official_path", str, "report")
    official_path = Path(official_name)
    try:
        official_st = official_path.lstat()
    except OSError as exc:
        raise BundleAuditError("official source path is missing") from exc
    if not stat.S_ISREG(official_st.st_mode) or official_path.is_symlink():
        raise BundleAuditError("official source must be an ordinary non-symlink file")
    official_bytes = official_path.read_bytes()
    identity = build.identify_official(official_bytes)
    for key in ("identity", "model", "sha256", "bytes"):
        if official.get(key) != identity.get(key):
            raise BundleAuditError("official source identity differs from exact pinned package")
    _validate_source_identity(report, identity, workspace_root)
    reported_archive = report.get("candidate_archive_path")
    if not isinstance(reported_archive, str) or Path(reported_archive).resolve() != Path(archive_path).resolve():
        raise BundleAuditError("candidate archive path differs from explicit audit input")
    return candidate_path, candidate, official_path, official_bytes, identity


def _audit_dispatch_header(bundle_root, dispatcher, return_helper, expected_rows, identity, rootfs_tree):
    """Compile a tiny host probe so header initializers are checked as C data."""
    header_path, header, header_st = _file(bundle_root, dispatcher.get("header_path"), "dispatcher.header_path")
    if dispatcher.get("header_sha256") != sha256(header):
        raise BundleAuditError("generated dispatcher header hash mismatch")
    if dispatcher.get("path") != "/usr/sbin/bridge-check":
        raise BundleAuditError("dispatcher runtime path must be fixed")
    binary_rel = _safe_relpath(dispatcher.get("binary_path"), "dispatcher.binary_path")
    binary_path = bundle_root.joinpath(*binary_rel.parts)
    try:
        binary_st = binary_path.lstat()
    except OSError as exc:
        raise BundleAuditError("dispatcher staged binary is missing") from exc
    if not stat.S_ISREG(binary_st.st_mode) or binary_path.is_symlink():
        raise BundleAuditError("dispatcher staged binary must be a regular file")
    binary = binary_path.read_bytes()
    if (dispatcher.get("sha256") != sha256(binary) or dispatcher.get("mode") != _mode(binary_st) or
            dispatcher.get("bytes") != len(binary) or
            _mode(binary_st) not in DISPATCHER_MODES):
        raise BundleAuditError("dispatcher binary pin or executable mode mismatch")
    if dispatcher.get("records_count") != len(expected_rows):
        raise BundleAuditError("dispatcher record count differs from provider set")
    # The probe reads the actual generated array using the production ABI. It
    # keeps the audit independent from string parsing of C initializers.
    probe = r'''#include <stdio.h>
#include "bridge_dispatch.h"
#include "bridge_provider_bundle.h"
int main(void) {
  printf("ABI\t%u\t%u\t%s\t%s\n", BI_PROVIDER_BUNDLE_ABI_VERSION,
         BI_PROVIDER_COUNT, BI_PROVIDER_SOURCE_SHA256, BI_PROVIDER_PRODUCT);
  for (unsigned i=0;i<BI_PROVIDER_COUNT;i++) {
    const bdi_provider_record *r=&bootbridge_provider_records[i];
    printf("R\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%s\t%s\t%u\t%s\t%s\t%u\n",
      r->record_id,r->hardware_class,r->source_sha256,r->product,r->route,
      r->route_compatible,r->source_revision_sha256,r->stock_kernel_sha256,
      r->bridge_kernel_sha256,r->nand_dt_sha256,r->nand_model,
      r->jedec_bytes_observed,r->manufacturer_id,r->chip_id,r->capacity_mib,
      r->page_bytes,r->erase_bytes,r->raw_spare_bytes,r->mtd_oob_bytes,r->ecc_bits,
      r->provider_path,r->provider_sha256,r->provider_bytes,r->payload_path,
      r->payload_sha256,r->payload_bytes);
  }
  return 0;
}'''
    with tempfile.TemporaryDirectory(prefix="audit-provider-header-") as tmp:
        work = Path(tmp)
        inc = work / "bridge_provider_bundle.h"
        inc.write_bytes(header)
        src = work / "probe.c"
        src.write_text(probe, encoding="ascii")
        project_src = Path(__file__).resolve().parents[1] / "src"
        exe = work / "probe"
        cp = subprocess.run([os.environ.get("HOSTCC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                             "-I", str(project_src), "-I", str(work), str(src), "-o", str(exe)],
                            capture_output=True, timeout=30)
        if cp.returncode:
            detail = cp.stderr.decode("utf-8", "replace")[:600]
            raise BundleAuditError("generated dispatcher header does not compile against production ABI: " + detail)
        run = subprocess.run([str(exe)], capture_output=True, timeout=10)
        if run.returncode:
            raise BundleAuditError("generated dispatcher header probe failed")
    lines = run.stdout.decode("ascii").splitlines()
    if not lines or len(lines) != len(expected_rows) + 1:
        raise BundleAuditError("compiled dispatcher header record count mismatch")
    top = lines[0].split("\t")
    if (len(top) != 5 or top[0] != "ABI" or top[1] != "1" or
            int(top[2]) != len(expected_rows) or top[3] != identity["sha256"] or
            top[4] != identity["model"]):
        raise BundleAuditError("compiled dispatcher bundle identity mismatch")
    fields = ("record_id", "hardware_class", "source_sha256", "product", "route",
              "route_compatible", "source_revision_sha256", "stock_kernel_sha256",
              "bridge_kernel_sha256", "nand_dt_sha256", "nand_model", "jedec_bytes_observed",
              "manufacturer_id", "chip_id", "capacity_mib", "page_bytes", "erase_bytes",
              "raw_spare_bytes", "mtd_oob_bytes", "ecc_bits", "provider_path",
              "provider_sha256", "provider_bytes", "payload_path", "payload_sha256", "payload_bytes")
    actual = []
    for line in lines[1:]:
        col = line.split("\t")
        if len(col) != len(fields) + 1 or col[0] != "R":
            raise BundleAuditError("compiled dispatcher header emitted malformed record")
        row = dict(zip(fields, col[1:]))
        for key in fields[11:20] + ("provider_bytes", "payload_bytes"):
            row[key] = int(row[key])
        actual.append(row)
    def canon(row):
        return tuple(str(row.get(field)) for field in fields)
    if sorted(map(canon, actual)) != sorted(map(canon, expected_rows)):
        raise BundleAuditError("compiled dispatcher records differ from recomputed provider closure")
    full_tuples = [tuple(row[key] for key in ("route_compatible", "nand_model",
        "jedec_bytes_observed", "manufacturer_id", "chip_id", "capacity_mib", "page_bytes",
        "erase_bytes", "raw_spare_bytes", "mtd_oob_bytes", "ecc_bits")) for row in actual]
    if len(set(full_tuples)) != len(full_tuples):
        raise BundleAuditError("compiled provider table contains an indistinguishable full runtime tuple")
    project = Path(__file__).resolve().parents[1]
    matcher_env = dict(os.environ)
    matcher_env["CC"] = os.environ.get("HOSTCC", "cc")
    matcher = subprocess.run([sys.executable, str(project / "tests/test_bridge_dispatch.py")],
                             capture_output=True, timeout=60, env=matcher_env)
    if matcher.returncode:
        raise BundleAuditError("production dispatcher matcher tests failed")
    _rebuild_dispatcher(bundle_root, dispatcher, return_helper, header, binary)
    return {"header_sha256": sha256(header), "dispatcher_sha256": sha256(binary),
            "records": len(actual), "header_compiled": True,
            "matcher_tests": "PASS_NO_DEFAULT_UNKNOWN_OR_AMBIGUOUS_FALLBACK",
            "source_rebuild": "PASS_BYTE_EXACT"}


def _rebuild_dispatcher(bundle_root, dispatcher, return_helper, header_bytes, final_binary):
    compile_meta = _required(dispatcher, "compile", dict, "dispatcher")
    project = Path(__file__).resolve().parents[1]
    source_files = {
        "bridge_dispatch.c": project / "src/bridge_dispatch.c",
        "bridge_oem_return_exec.c": project / "src/bridge_oem_return_exec.c",
        "bridge_dispatch.h": project / "src/bridge_dispatch.h",
        "bridge_sha256.h": project / "src/bridge_sha256.h",
    }
    declared = compile_meta.get("sources")
    if not isinstance(declared, dict) or set(declared) != set(source_files):
        raise BundleAuditError("dispatcher source hash set is incomplete")
    for name, path in source_files.items():
        if not path.is_file() or declared[name] != sha256(path.read_bytes()):
            raise BundleAuditError("dispatcher source hash differs from frozen report: " + name)
    command = compile_meta.get("command")
    compiler = compile_meta.get("compiler_path")
    if (not isinstance(command, list) or not command or not isinstance(compiler, str) or
            not _same_executable(command[0], compiler)):
        raise BundleAuditError("recorded dispatcher compiler is unavailable or malformed")
    compiler = str(Path(compiler).resolve(strict=True))
    try:
        version = subprocess.run([compiler, "--version"], check=True, capture_output=True,
                                 text=True, timeout=15).stdout.splitlines()[0]
    except (OSError, subprocess.SubprocessError, IndexError) as exc:
        raise BundleAuditError("cannot verify dispatcher compiler version") from exc
    if version != compile_meta.get("compiler_version"):
        raise BundleAuditError("dispatcher compiler version differs from report")
    # Require the exact reviewed command shape, while replacing only its output
    # and include/source locations with this independently extracted header and
    # the frozen local project source.
    expected_flags = ["-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static", "-s",
                      "-fno-ident", "-Wl,--build-id=none"]
    if command[1:10] != expected_flags:
        raise BundleAuditError("dispatcher compile flags differ from reviewed command")
    if (len(command) != 20 or command[10] != "-I" or command[12] != "-I" or
            command[18] != "-o" or Path(command[16]).name != "bridge_dispatch.c" or
            Path(command[17]).name != "bridge_oem_return_exec.c"):
        raise BundleAuditError("dispatcher compile command has unexpected arguments")
    if not isinstance(return_helper, dict):
        raise BundleAuditError("dispatcher return helper metadata is missing")
    if (command[14] != '-DBR_RETURN_EXEC_SHA256="' + return_helper.get("sha256", "") + '"' or
            command[15] != "-DBR_RETURN_EXEC_BYTES=%du" % return_helper.get("bytes", -1)):
        raise BundleAuditError("dispatcher return-helper compile-time pins differ from report")
    log_value = compile_meta.get("compile_log")
    if not isinstance(log_value, str):
        raise BundleAuditError("dispatcher compile log path is missing")
    log_path = Path(log_value)
    try:
        log_relative = log_path.resolve().relative_to(bundle_root)
    except ValueError as exc:
        raise BundleAuditError("dispatcher compile log is outside the frozen bundle evidence") from exc
    _, log_bytes, _ = _file(bundle_root, log_relative.as_posix(), "dispatcher.compile_log")
    if compile_meta.get("compile_log_sha256") != sha256(log_bytes):
        raise BundleAuditError("dispatcher compile log hash mismatch")
    with tempfile.TemporaryDirectory(prefix="audit-dispatch-rebuild-") as tmp:
        work = Path(tmp)
        include = work / "include"
        include.mkdir()
        (include / "bridge_provider_bundle.h").write_bytes(header_bytes)
        output = work / "bridge-check"
        try:
            bundle_build.compile_dispatcher(compiler, include, output, work / "dispatcher-rebuild.log",
                                            return_helper.get("sha256"), return_helper.get("bytes"))
        except Exception as exc:
            raise BundleAuditError("dispatcher source rebuild failed") from exc
        if output.read_bytes() != final_binary:
            raise BundleAuditError("dispatcher binary is not byte-identical to the source/header rebuild")


def _audit_fresh_candidate_bundle(report, bundle_root, archive_path, archive_sha256,
                                 manifest_sha256, descriptors_sha256, payload_report_path,
                                 root_binding_gate_config):
    if (report.get("status") != "OFFLINE_LAB_CANDIDATE" or
            report.get("device_operations") is not False or
            report.get("hardware_validation") != "NOT_EXECUTED" or
            report.get("physical_install_qualification") != "NOT_GRANTED"):
        raise BundleAuditError("fresh bundle status or qualification fields are invalid")
    pins = (archive_sha256, manifest_sha256, descriptors_sha256)
    if any(not isinstance(pin, str) for pin in pins):
        raise BundleAuditError("fresh archive audit requires three explicit SHA-256 pins")
    try:
        parsed = candidate_manifest.read_candidate_archive_v2(Path(archive_path), *pins)
    except Exception as exc:
        raise BundleAuditError("pinned fresh candidate archive rejected") from exc
    if (report.get("candidate_archive_sha256") != pins[0] or
            report.get("candidate_manifest_sha256") != pins[1] or
            report.get("candidate_descriptors_sha256") != pins[2] or
            report.get("candidate_archive_bytes") != parsed["archive_bytes"] or
            report.get("candidate_manifest_bytes") != parsed["manifest_bytes"] or
            report.get("candidate_descriptors_bytes") != parsed["candidates_bytes"] or
            report.get("candidate_archive_path") != str(Path(archive_path).resolve())):
        raise BundleAuditError("fresh archive report pins/path differ from audit inputs")
    official_path = Path(report.get("official_path", ""))
    try:
        st = official_path.lstat()
    except OSError as exc:
        raise BundleAuditError("fresh official source path is missing") from exc
    if not stat.S_ISREG(st.st_mode) or official_path.is_symlink():
        raise BundleAuditError("fresh official source is not an ordinary file")
    official_bytes = official_path.read_bytes()
    identity = build.identify_official(official_bytes)
    _validate_source_identity(report, identity, Path(__file__).resolve().parents[3])
    official = report.get("official")
    if not isinstance(official, dict) or any(official.get(k) != identity.get(k)
            for k in ("identity", "model", "sha256", "bytes")):
        raise BundleAuditError("fresh official source identity mismatch")
    profile_sets = report.get("profile_sets")
    if not isinstance(profile_sets, dict) or len(profile_sets) != 1:
        raise BundleAuditError("fresh profile-set inventory must be keyed by exact set ID")
    source_nand = None
    for set_id, item in profile_sets.items():
        if isinstance(item, dict) and item.get("source_sha256") == identity["sha256"]:
            if set_id != item.get("profile_set_id"):
                raise BundleAuditError("fresh profile-set key differs from its identity")
            source_nand = item
            break
    if source_nand is None:
        raise BundleAuditError("fresh NAND source profile set is missing")
    variants = source_nand.get("source_variants")
    profile_set_id, profile_ids = nand_schema.profile_set_id(identity["sha256"], variants)
    catalog = nand_schema.profiles()
    if (source_nand.get("profile_set_id") != profile_set_id or
            source_nand.get("catalog_sha256") != catalog["catalog_sha256"] or
            report.get("nand_catalog_sha256") != catalog["catalog_sha256"] or
            source_nand.get("profile_ids") != profile_ids):
        raise BundleAuditError("fresh NAND profile inventory is not source/catalog bound")
    archive_descriptors = parsed["candidates"]
    archive_candidate_rows = []
    for desc in archive_descriptors:
        files = desc["files"]
        archive_candidate_rows.append({"candidate_id": desc["candidate_id"], "board": desc["board"],
            "nand_profile_id": desc["nand_profile_id"], "load_ecc_bits": desc["load_ecc_bits"],
            "storage_ecc_bits": desc["storage_ecc_bits"],
            "partition_sha256": files["appsbl.partition.bin"]["sha256"],
            "mbn_sha256": files["appsbl.mbn"]["sha256"],
            "load_sha256": desc["container"]["load"]["sha256"],
            "partition_bytes": files["appsbl.partition.bin"]["bytes"]})
    candidate_set_id = nand_schema.candidate_set_id(identity["sha256"], pins[0], archive_candidate_rows)
    if report.get("candidate_set_id") != candidate_set_id:
        raise BundleAuditError("fresh candidate-set digest mismatch")
    if report.get("candidate_inventory") != archive_candidate_rows:
        raise BundleAuditError("reported full candidate inventory differs from pinned archive descriptors")
    source_boards = bundle_build.source_boards(identity)
    source_variants = bundle_build.SOURCE_TABLE_VARIANTS[identity["sha256"]]
    table_mask = 0
    for variant in source_variants:
        table_mask |= nand_schema.TABLE_MASKS[variant]
    raw_profiles = {p["profile_id"]: p for p in catalog["profiles"]}
    seen_formats = set()
    for desc in archive_descriptors:
        key = (desc["board"], desc["nand_profile_id"], desc["load_ecc_bits"], desc["storage_ecc_bits"])
        profile = raw_profiles.get(desc["nand_profile_id"])
        if desc["load_ecc_bits"] != desc["storage_ecc_bits"]:
            raise BundleAuditError("fresh candidate LOAD ECC differs from runtime storage ECC")
        if key in seen_formats or profile is None or desc["board"] not in source_boards:
            raise BundleAuditError("fresh candidate archive has duplicate or unknown board/profile format")
        seen_formats.add(key)
    expected_archive_formats = archive_candidate_format_matrix(raw_profiles, source_boards)
    if seen_formats != expected_archive_formats:
        missing = sorted(expected_archive_formats - seen_formats)
        unexpected = sorted(seen_formats - expected_archive_formats)
        raise BundleAuditError("fresh archive differs from full U-Boot/QPIC NAND matrix: "
            f"expected={len(expected_archive_formats)} actual={len(seen_formats)} "
            f"missing={missing[:8]} unexpected={unexpected[:8]}")
    expected_formats = set()
    for profile_id in profile_ids:
        profile = raw_profiles[profile_id]
        formats = [ecc for ecc, bit, field in ((4,1,"appsbl_ecc4_source_mask"),(8,2,"appsbl_ecc8_source_mask"))
                   if profile["appsbl_ecc_mode_mask"] & bit and profile[field] & table_mask and
                   profile["runtime_ecc_mode_mask"] & bit and profile["kernel_ecc_mode_mask"] & bit]
        for board in source_boards:
            for ecc in formats:
                expected_formats.add((board, profile_id, ecc, ecc))
    if {key for key in seen_formats if key in expected_formats} != expected_formats:
        raise BundleAuditError("fresh candidate archive lacks source-derived board/profile/load/storage coverage")
    expected_omitted = [{"candidate_id": d["candidate_id"], "board": d["board"],
        "nand_profile_id": d["nand_profile_id"], "reason": "NO_EXACT_SOURCE_KERNEL_PROFILE"}
        for d in archive_descriptors
        if (d["board"], d["nand_profile_id"], d["load_ecc_bits"], d["storage_ecc_bits"])
           not in expected_formats]
    if report.get("source_ineligible_candidates") != expected_omitted or report.get(
            "candidate_archive_candidate_count") != len(archive_descriptors):
        raise BundleAuditError("source-ineligible archive candidate inventory is absent or changed")
    descriptors = [d for d in archive_descriptors
        if (d["board"], d["nand_profile_id"], d["load_ecc_bits"], d["storage_ecc_bits"])
           in expected_formats]
    rows = report.get("providers")
    if not isinstance(rows, list) or len(rows) != len(descriptors) or not 1 <= len(rows) <= 160:
        raise BundleAuditError("fresh provider rows do not exactly cover archive candidate rows")
    by_id = {d["candidate_id"]: d for d in descriptors}
    seen_records, seen_provider_paths, seen_storage = set(), set(), {}
    board_bits = {"A": 1, "B": 2, "C": 4, "D": 8}
    dispatcher_compile = _required(_required(report, "dispatcher", dict, "report"),
                                   "compile", dict, "dispatcher")
    provider_compiler = dispatcher_compile.get("compiler_path")
    provider_compiler_version = dispatcher_compile.get("compiler_version")
    if (not isinstance(provider_compiler, str) or not Path(provider_compiler).is_file() or
            not isinstance(provider_compiler_version, str)):
        raise BundleAuditError("fresh ARM provider compiler identity is missing from the report")
    provider_rebuilds = []
    for row in rows:
        if not isinstance(row, dict):
            raise BundleAuditError("fresh provider row is not an object")
        desc = by_id.get(row.get("candidate_id"))
        if desc is None or row.get("candidate_set_id") != candidate_set_id or row.get("profile_set_id") != profile_set_id:
            raise BundleAuditError("fresh provider row candidate/profile set identity mismatch")
        board = desc["board"]
        if (row.get("hardware_class") != board or row.get("source_sha256") != identity["sha256"] or
                row.get("product") != identity["model"] or row.get("record_id") in seen_records):
            raise BundleAuditError("fresh provider row source/class/record identity mismatch")
        seen_records.add(row["record_id"])
        payload = row.get("payload")
        if not isinstance(payload, dict) or not isinstance(payload.get("descriptor"), dict):
            raise BundleAuditError("fresh provider payload descriptor is absent")
        provider, payload_meta = row.get("provider"), row.get("payload")
        if not isinstance(provider, dict) or not isinstance(payload_meta, dict):
            raise BundleAuditError("fresh provider artifact pins are missing")
        provider_rel = provider.get("path")
        if (not isinstance(provider_rel, str) or not provider_rel.startswith("/") or
                ".." in Path(provider_rel).parts or provider_rel in seen_provider_paths):
            raise BundleAuditError("fresh provider artifact path is unsafe or reused")
        seen_provider_paths.add(provider_rel)
        provider_target = bundle_root / "rootfs" / provider_rel.lstrip("/")
        try:
            provider_st = provider_target.lstat()
        except OSError as exc:
            raise BundleAuditError("fresh provider artifact is missing") from exc
        if not stat.S_ISREG(provider_st.st_mode) or provider_target.is_symlink():
            raise BundleAuditError("fresh provider artifact is not a regular file")
        provider_bytes = provider_target.read_bytes()
        if (len(provider_bytes) != provider.get("bytes") or sha256(provider_bytes) != provider.get("sha256") or
                (provider_st.st_mode & 0o777) != provider.get("mode")):
            raise BundleAuditError("fresh provider artifact differs from exact report pins")
        if (not isinstance(payload_meta.get("descriptor"), dict) or
                payload_meta["descriptor"].get("candidate_id") != desc["candidate_id"]):
            raise BundleAuditError("fresh payload row does not bind its exact archive descriptor")
        full_candidate = candidate_manifest.extract_candidate_partition_v2(Path(archive_path), desc)
        base_data, patch_header, patch_metadata, expected_storage = audit.fresh_payload_storage_contract(
            archive_path, archive_descriptors, payload_meta["descriptor"], full_candidate)
        storage = payload_meta.get("storage")
        if (not isinstance(storage, dict) or storage != expected_storage or
                payload_meta.get("path") != storage.get("path") or
                payload_meta.get("sha256") != desc["files"]["appsbl.partition.bin"]["sha256"] or
                payload_meta.get("bytes") != desc["files"]["appsbl.partition.bin"]["bytes"]):
            raise BundleAuditError("fresh candidate final identity or base-patch storage metadata mismatch")
        if (storage.get("path") != "/etc/bridge/provider-payload-bases/" + storage["sha256"] + ".bin" or
                payload_meta.get("mode") != PAYLOAD_MODE):
            raise BundleAuditError("fresh payload base path/mode is not content-addressed")
        prior_storage = seen_storage.get(storage["path"])
        storage_identity = (storage["sha256"], storage["bytes"], payload_meta["mode"])
        if prior_storage is not None and prior_storage != storage_identity:
            raise BundleAuditError("shared base path is reused for different bytes or mode")
        seen_storage[storage["path"]] = storage_identity
        base_target = bundle_root / "rootfs" / storage["path"].lstrip("/")
        try:
            base_st = base_target.lstat()
        except OSError as exc:
            raise BundleAuditError("fresh payload base is missing") from exc
        if not stat.S_ISREG(base_st.st_mode) or base_target.is_symlink():
            raise BundleAuditError("shared payload base is not a regular file")
        base_on_disk = base_target.read_bytes()
        if (len(base_on_disk) != storage["bytes"] or sha256(base_on_disk) != storage["sha256"] or
                (base_st.st_mode & 0o777) != PAYLOAD_MODE or base_on_disk != base_data):
            raise BundleAuditError("shared payload base differs from exact validated archive bytes")
        source_report_ref = _required(row, "provider_report", dict, "provider")
        source_report_rel = _safe_relpath(source_report_ref.get("path"), "provider_report.path")
        source_patch_header = bundle_root.joinpath(*source_report_rel.parts).parent / "bridge_payload_patch.h"
        if (source_patch_header.read_bytes() != patch_header.encode("ascii") or
                patch_metadata["reconstructed_sha256"] != payload_meta["sha256"]):
            raise BundleAuditError("provider patch header does not independently reconstruct the archive candidate")
        profile_rows = nand_schema.runtime_profiles(source_nand["source_variants"], board_bits[board])
        matching = [p for p in profile_rows if p["profile_id"] == desc["nand_profile_id"] and
                    p["ecc_bits"] == desc["storage_ecc_bits"]]
        if (desc["load_ecc_bits"] != desc["storage_ecc_bits"] or not matching or
                row.get("nand_profiles") != matching):
            raise BundleAuditError("fresh provider NAND tuple differs from exact source/runtime profile")
        if "appsbl.elf" in desc.get("files", {}):
            profile = matching[0]
            candidate = dict(desc)
            candidate["build_profile"] = build.candidate_build_profile(
                candidate, board, identity["model"], matching)
            if payload.get("descriptor") != candidate:
                raise BundleAuditError("fresh provider descriptor differs from archive and source-specific ENV policy")
            expected_bfi, expected_bii = build.candidate_profile_codegen.render_headers(
                candidate, candidate_manifest.extract_candidate_member_v2(
                    Path(archive_path), desc, "appsbl.elf"), matching,
                candidate["build_profile"], candidate["bss_metadata"])
            header_rows = row.get("candidate_profile_headers")
            if (not isinstance(header_rows, list) or len(header_rows) != 2 or
                    any(not isinstance(x, dict) for x in header_rows) or
                    [Path(x.get("path", "")).name for x in header_rows] !=
                    ["bfi_candidate_profile.generated.h", "bii_candidate_policy.generated.h"]):
                raise BundleAuditError("fresh candidate BFI/ENV compile headers are not pinned")
            expected_headers = [expected_bfi.encode("ascii"), expected_bii.encode("ascii")]
            for header_row, expected in zip(header_rows, expected_headers):
                rel = _safe_relpath(header_row.get("path"), "candidate profile header path")
                header_path = bundle_root.joinpath(*rel.parts)
                try:
                    st = header_path.lstat()
                except OSError as exc:
                    raise BundleAuditError("fresh candidate profile header is missing") from exc
                data = header_path.read_bytes()
                if (not stat.S_ISREG(st.st_mode) or header_path.is_symlink() or
                        (st.st_mode & 0o777) != 0o600 or data != expected or
                        len(data) != header_row.get("bytes") or sha256(data) != header_row.get("sha256")):
                    raise BundleAuditError("fresh candidate profile header differs from independently rendered contract")
            components = build.official_bootchain_components(official_bytes, identity)
            expected_first = build.generated_first_install_profile_header(
                board, identity, components,
                nand_profile=dict(profile, accepted_mtd_oob_mask=sum(
                    {64:1, 128:2, 256:4}[p["mtd_oob_bytes"]] for p in matching)),
                candidate_id=desc["candidate_id"]).encode("ascii")
            profile_header = row.get("profile_header")
            if not isinstance(profile_header, dict):
                raise BundleAuditError("fresh first-install profile header is unpinned")
            rel = _safe_relpath(profile_header.get("path"), "first-install profile header path")
            path = bundle_root.joinpath(*rel.parts)
            try:
                st = path.lstat()
            except OSError as exc:
                raise BundleAuditError("fresh first-install profile header is missing") from exc
            data = path.read_bytes()
            if (not stat.S_ISREG(st.st_mode) or path.is_symlink() or (st.st_mode & 0o777) != 0o600 or
                    data != expected_first or sha256(data) != profile_header.get("sha256")):
                raise BundleAuditError("fresh first-install profile header differs from source extraction")
            try:
                source_root = _audit_provider_source(row, board, identity, desc, bundle_root,
                    official_path, archive_path, payload_report_path, *pins)
                provider_rebuilds.append(_rebuild_fresh_provider(source_root, row, board,
                    identity, candidate, official_bytes, archive_path, archive_descriptors,
                    payload_report_path, matching, components, provider_compiler,
                    provider_compiler_version))
            except Exception as exc:
                raise BundleAuditError("fresh child provider audit failed for " +
                    desc["candidate_id"] + ": " + str(exc)) from exc
    if len(provider_rebuilds) != len(rows):
        raise BundleAuditError("fresh bundle did not independently rebuild every provider")
    combined_image = _audit_fresh_combined_image(report, bundle_root, official_path,
        official_bytes, identity, rows)
    if not root_binding_gate_config:
        raise BundleAuditError("independent audit requires --root-binding-gate-config")
    final_ubi = bundle_root / "bridge.ubi"
    try:
        final_ubi_bytes = final_ubi.read_bytes()
    except OSError as exc:
        raise BundleAuditError("final bridge UBI is unavailable for live gate audit") from exc
    if sha256(final_ubi_bytes) != combined_image.get("ubi_sha256"):
        raise BundleAuditError("final bridge UBI differs from the independently extracted outer image")
    contract = report.get("root_binding_contract", {}).get("contract")
    gate_header = bundle_root / "dispatcher-include/bridge_root_binding_contract.generated.h"
    try:
        # Keep independent guest logs, capsule, and receipt as audit evidence.
        # mkdtemp creates a fresh private directory and never overwrites an older run.
        audit_gate_root = Path(tempfile.mkdtemp(prefix="root-binding-audit-",
                                                dir=str(bundle_root)))
        gate_recheck = root_binding_contract.run_live_gate(root_binding_gate_config,
            identity["model"], contract, gate_header, final_ubi,
            audit_gate_root / "gate", relative_to=bundle_root)
        if root_binding_contract.gate_binding_key(report.get("root_binding_gate")) != \
                root_binding_contract.gate_binding_key(gate_recheck):
            raise BundleAuditError("independent live root binding gate differs from build receipt")
    except (OSError, ValueError, root_binding_contract.RootBindingContractError) as exc:
        if isinstance(exc, BundleAuditError):
            raise
        raise BundleAuditError("independent live GlueBI/sysfs gate failed: " + str(exc)) from exc
    helper = report.get("return_helper")
    if report.get("return_policy") != "SOURCE_BOUND_TRIAL_RETURN_HELPER; NOT_INSTALL_READY":
        raise BundleAuditError("source-bound return helper status is absent or overstated")
    if not isinstance(helper, dict) or helper.get("path") != "/usr/sbin/bridge-oem-return":
        raise BundleAuditError("source-bound return helper identity is missing")
    helper_binary_rel = _safe_relpath(helper.get("binary_path"), "return_helper.binary_path")
    helper_binary = bundle_root.joinpath(*helper_binary_rel.parts)
    try:
        helper_st = helper_binary.lstat()
    except OSError as exc:
        raise BundleAuditError("source-bound return helper binary is missing") from exc
    helper_data = helper_binary.read_bytes()
    if (not stat.S_ISREG(helper_st.st_mode) or helper_binary.is_symlink() or
            sha256(helper_data) != helper.get("sha256") or len(helper_data) != helper.get("bytes") or
            (helper_st.st_mode & 0o777) != 0o555 or helper.get("mode") != 0o555):
        raise BundleAuditError("source-bound return helper binary does not match its immutable pin")
    registry_rel = _safe_relpath(helper.get("registry_header_path"), "return_helper.registry_header_path")
    manifest_rel = _safe_relpath(helper.get("manifest_header_path"), "return_helper.manifest_header_path")
    registry_path, manifest_path = bundle_root.joinpath(*registry_rel.parts), bundle_root.joinpath(*manifest_rel.parts)
    registry_bytes, manifest_bytes = registry_path.read_bytes(), manifest_path.read_bytes()
    if (sha256(registry_bytes) != helper.get("registry_header_sha256") or
            sha256(manifest_bytes) != helper.get("manifest_header_sha256")):
        raise BundleAuditError("source-bound return helper generated header digest mismatch")
    catalog_rel = _safe_relpath(helper.get("catalog_header_path"), "return_helper.catalog_header_path")
    catalog_header = bundle_root.joinpath(*catalog_rel.parts)
    catalog_bytes = catalog_header.read_bytes()
    catalog_sha = nand_schema.profiles()["catalog_sha256"]
    if (catalog_bytes != nand_schema.CATALOG.read_bytes() or
            sha256(catalog_bytes) != catalog_sha or
            helper.get("catalog_header_sha256") != catalog_sha):
        raise BundleAuditError("return helper NAND catalog header differs from canonical source")
    stock_fit_size = struct.unpack_from(">I", official_bytes, 164)[0]
    stock_fit_nodes = vendor_fit.fdt_props(official_bytes[160:160 + stock_fit_size])
    stock_ubi_rows = _stock_ubi_rows(stock_fit_nodes)
    if len(stock_ubi_rows) != 1:
        raise BundleAuditError("source helper return pool requires one exact official UBI")
    helper_root_contract = root_binding_contract.derive(identity["sha256"], stock_ubi_rows[0][1])
    helper_root_header = bundle_root / "dispatcher-include/bridge_root_binding_contract.generated.h"
    expected_helper_root_header = root_binding_contract.render_header(helper_root_contract).encode("ascii")
    helper_root_info = report.get("root_binding_contract")
    if (not isinstance(helper_root_info, dict) or helper_root_info.get("contract") != helper_root_contract or
            helper.get("root_binding_contract_header_path") != "dispatcher-include/bridge_root_binding_contract.generated.h" or
            helper.get("root_binding_contract_header_sha256") != sha256(expected_helper_root_header) or
            helper.get("root_binding_contract_sha256") != root_binding_contract.contract_sha256(helper_root_contract) or
            not helper_root_header.is_file() or helper_root_header.is_symlink() or
            helper_root_header.read_bytes() != expected_helper_root_header):
        raise BundleAuditError("source return helper rootfs geometry contract/header is not source-bound")
    with tempfile.TemporaryDirectory(prefix="audit-return-pool-") as pool_tmp:
        pool_tmp = Path(pool_tmp)
        slot_bytes = 40 * 1024 * 1024
        stock_ubi = stock_ubi_rows[0][1]
        if not 0 < len(stock_ubi) <= slot_bytes:
            raise BundleAuditError("source helper stock UBI exceeds its fixed slot")
        padded = stock_ubi + b"\xff" * (slot_bytes - len(stock_ubi))
        padded_path = pool_tmp / "source-slot.ubi"
        padded_path.write_bytes(padded)
        try:
            table_sha = build.public_autoresize_source_table(padded_path)
        except Exception as exc:
            raise BundleAuditError("source UBI autoresize table fails the reviewed normalizer") from exc
        if (sha256(padded) != helper.get("source_slot_sha256") or
                table_sha != helper.get("source_volume_table_sha256")):
            raise BundleAuditError("return helper pool source slot/table pin mismatch")
        source_runtime = nand_schema.runtime_profiles(
            bundle_build.SOURCE_TABLE_VARIANTS[identity["sha256"]], 0)
        geometries = sorted({(p["mtd_oob_bytes"], p["ecc_bits"]) for p in source_runtime})
        manifests = []
        for offset in (0x900000, 0x3100000):
            for oob_bytes, ecc_bits in geometries:
                manifests.append(build.oem_manifest.build_manifest(padded_path,
                    expected_ubi_sha256=sha256(padded), candidate_sha256=identity["sha256"],
                    observed_geometry={"physical_offset": offset, "size_bytes": slot_bytes,
                        "erase_bytes": 131072, "page_bytes": 2048,
                        "oob_bytes": oob_bytes, "ecc_bits": ecc_bits}))
        pool_entries = build.oem_pool_entries(manifests, identity, exit_policy_override=1)
        pool_entries.extend(dict(entry, table_policy=2, alias=entry["alias"] + 10000)
                            for entry in tuple(pool_entries))
        expected_manifest_header = build.oem_manifest.c_pool_header(pool_entries).encode("ascii")
        if manifest_bytes != expected_manifest_header:
            raise BundleAuditError("return helper pool differs from independently normalized source UBI rules")
    kernel_report = report.get("kernel")
    if not isinstance(kernel_report, dict):
        raise BundleAuditError("source-bound helper kernel metadata is missing")
    stock_fit_rel = _safe_relpath(kernel_report.get("stock_fit_path"), "kernel.stock_fit_path")
    bridge_fit_rel = _safe_relpath(kernel_report.get("bridge_fit_path"), "kernel.bridge_fit_path")
    stock_fit_path = bundle_root.joinpath(*stock_fit_rel.parts)
    bridge_fit_path = bundle_root.joinpath(*bridge_fit_rel.parts)
    stock_fit_bytes, bridge_fit_bytes = stock_fit_path.read_bytes(), bridge_fit_path.read_bytes()
    with tempfile.TemporaryDirectory(prefix="audit-stock-kernel-ubi-") as ubi_tmp:
        stock_ubi_path = Path(ubi_tmp) / "official.ubi"
        stock_ubi_path.write_bytes(stock_ubi_rows[0][1])
        official_ubi_kernel = vendor_ubi.parse(stock_ubi_path)[1]["kernel"]
    if (stock_fit_bytes != official_ubi_kernel or
            sha256(stock_fit_bytes) != kernel_report.get("stock_fit_sha256") or
            sha256(bridge_fit_bytes) != kernel_report.get("bridge_fit_sha256")):
        raise BundleAuditError("return helper stock/bridge FIT inputs differ from exact source/report pins")
    unique_routes = {}
    for row in rows:
        route = row.get("route")
        computed = build.return_source.metadata(stock_fit_bytes, bridge_fit_bytes,
            identity["sha256"], route=route)
        if row.get("return_source") != computed:
            raise BundleAuditError("provider route metadata differs from independently parsed FIT inputs")
        unique_routes[route] = computed
    components = build.official_bootchain_components(official_bytes, identity)
    source_variants = bundle_build.SOURCE_TABLE_VARIANTS[identity["sha256"]]
    source_table_mask = sum(nand_schema.TABLE_MASKS[v] for v in source_variants)
    expected_registry = build.return_source.c_return_contract_header(
        [unique_routes[key] for key in sorted(unique_routes)],
        {identity["sha256"]: components}, nand_schema.profiles()["profiles"],
        source_table_mask=source_table_mask).encode("ascii")
    if registry_bytes != expected_registry:
        raise BundleAuditError("return source/profile registry differs from exact source, FIT, and NAND masks")
    compiler = report.get("dispatcher", {}).get("compile", {}).get("compiler_path")
    if not isinstance(compiler, str) or not Path(compiler).is_file():
        raise BundleAuditError("return helper rebuild compiler identity is missing")
    dispatcher = report.get("dispatcher")
    if not isinstance(dispatcher, dict):
        raise BundleAuditError("ABI-v2 dispatcher metadata is missing")
    expected_dispatch_header = build.generated_provider_bundle_header(
        identity["sha256"], identity["model"], [bundle_build.c_record(row) for row in rows]).encode("ascii")
    dispatch_header_rel = _safe_relpath(dispatcher.get("header_path"), "dispatcher.header_path")
    dispatch_header_path = bundle_root.joinpath(*dispatch_header_rel.parts)
    dispatch_header_bytes = dispatch_header_path.read_bytes()
    if (dispatch_header_bytes != expected_dispatch_header or
            sha256(dispatch_header_bytes) != dispatcher.get("header_sha256")):
        raise BundleAuditError("dispatcher header differs from independently rendered provider rows")
    project = Path(__file__).resolve().parents[1]
    source_paths = [project / "src" / name for name in (
        "bridge_check.c", "bridge_install.c", "bridge_version.c", "bridge_oem_slot.c",
        "bridge_backup.c", "bridge_root_binding.c", "bridge_first_install.c",
        "bridge_install_identity.c", "bridge_mtd_observer.c")]
    with tempfile.TemporaryDirectory(prefix="audit-return-helper-") as temp:
        rebuilt = Path(temp) / "bridge-oem-return"
        command = [compiler, "-std=c99", "-Wall", "-Wextra", "-Werror", "-Os", "-static", "-s",
            "-fno-ident", "-Wl,--build-id=none", "-DBRIDGE_RETURN_ONLY=1",
            "-DBI_HAVE_PAYLOAD=0", '-DBI_RETURN_SOURCE_REGISTRY_HEADER="bridge_oem_return_registry.h"',
            '-DBI_OEM_MANIFEST_HEADER="bridge_oem_return_manifest.h"',
            root_binding_contract.COMPILE_FLAG,
            f'-DBI_NAND_CATALOG_SHA256="{catalog_sha}"', "-I", bundle_root / "dispatcher-include",
            "-I", project / "src", *source_paths, "-o", rebuilt]
        try:
            subprocess.run([str(v) for v in command], check=True, capture_output=True, timeout=60)
        except (OSError, subprocess.SubprocessError) as exc:
            raise BundleAuditError("independent return helper rebuild failed") from exc
        if rebuilt.read_bytes() != helper_data:
            raise BundleAuditError("return helper differs from independent source/header rebuild")
        dispatcher_meta = report.get("dispatcher", {}).get("compile", {})
        dispatcher_compiler = dispatcher_meta.get("compiler_path")
        if dispatcher_compiler != compiler:
            raise BundleAuditError("dispatcher and return helper compiler identities differ")
        rebuilt_dispatcher = Path(temp) / "bridge-check"
        try:
            bundle_build.compile_dispatcher(dispatcher_compiler,
                bundle_root / "dispatcher-include", rebuilt_dispatcher,
                Path(temp) / "dispatcher-rebuild.log", helper.get("sha256"), helper.get("bytes"))
        except Exception as exc:
            raise BundleAuditError("independent dispatcher rebuild failed") from exc
        dispatcher_binary = bundle_root / "rootfs/usr/sbin/bridge-check"
        if rebuilt_dispatcher.read_bytes() != dispatcher_binary.read_bytes():
            raise BundleAuditError("dispatcher differs from independent ABI-v2 source/header/helper rebuild")
    recorded_gate = dispatcher.get("production_gate")
    if not isinstance(recorded_gate, dict):
        raise BundleAuditError("production dispatcher complete-record gate receipt is missing")
    try:
        rebuilt_gate = bundle_build.run_production_dispatch_gate(
            identity, rows, len(descriptors), cc="cc")
    except Exception as exc:
        raise BundleAuditError("independent production dispatcher complete-record gate failed") from exc
    if recorded_gate != rebuilt_gate:
        raise BundleAuditError("production dispatcher gate receipt differs from independent C runtime vectors")
    return {"schema": "bootbridge-provider-bundle-audit-v2", "status": "PASS",
        "bundle_status": "OFFLINE_LAB_CANDIDATE", "official_sha256": identity["sha256"],
        "candidate_archive_sha256": pins[0], "candidate_manifest_sha256": pins[1],
        "candidate_descriptors_sha256": pins[2],
        "candidate_archive_bytes": parsed["archive_bytes"],
        "candidate_manifest_bytes": parsed["manifest_bytes"],
        "candidate_descriptors_bytes": parsed["candidates_bytes"],
        "root_binding_contract": report["root_binding_contract"],
        "root_binding_gate": gate_recheck,
        "candidate_set_id": candidate_set_id,
        "combined_image": combined_image,
        "profile_sets": profile_sets, "providers": rows,
        "provider_rebuilds": provider_rebuilds,
        "candidate_inventory": archive_candidate_rows,
        "source_ineligible_candidates": expected_omitted,
        "candidate_archive_candidate_count": len(archive_descriptors),
        "provider_records": len(rows), "nand_profile_count": len(profile_ids),
        "hardware_validation": "NOT_EXECUTED", "physical_install_qualification": "NOT_GRANTED"}


def _audit_fresh_combined_image(report, bundle_root, official_path, official_bytes, identity, rows):
    """Audit the final OEM wrapper, shared UBI/FIT, and full SquashFS tree."""
    candidate_name = _required(report, "candidate_path", str, "report")
    candidate_path, candidate, _ = _file(bundle_root, candidate_name, "candidate_path")
    candidate_meta = _required(report, "candidate", dict, "report")
    if (candidate_meta.get("path") != candidate_name or candidate_meta.get("bytes") != len(candidate) or
            _hex_digest(candidate_meta.get("sha256"), "candidate.sha256") != sha256(candidate)):
        raise BundleAuditError("fresh combined candidate differs from its report pin")
    try:
        candidate_identity, outer, image_paths = audit.audit_wrapper(candidate, official_bytes)
    except Exception as exc:
        raise BundleAuditError("fresh final bundle wrapper rejected") from exc
    if candidate_identity != identity:
        raise BundleAuditError("fresh combined candidate identity changed")
    if len(candidate) > len(official_bytes):
        raise BundleAuditError("fresh combined candidate exceeds OEM upload capacity")
    fit_size = struct.unpack_from(">I", candidate, 164)[0]
    try:
        outer_fit = build.verify_fit(candidate[160:160 + fit_size])
    except Exception as exc:
        raise BundleAuditError("fresh combined outer FIT hashes rejected") from exc
    ubi_blob = outer[image_paths[0]]["data"]

    official_fit_len = struct.unpack_from(">I", official_bytes, 164)[0]
    try:
        stock_fit = build.verify_fit(official_bytes[160:160 + official_fit_len])
    except Exception as exc:
        raise BundleAuditError("fresh official FIT hashes rejected") from exc
    stock_ubi_rows = _stock_ubi_rows(stock_fit)
    if len(stock_ubi_rows) != 1:
        raise BundleAuditError("fresh official source does not contain one exact stock UBI image")
    expected_root_contract = root_binding_contract.derive(identity["sha256"], stock_ubi_rows[0][1])
    try:
        built_root_geometry = root_binding_contract.verify_same_source_and_built(
            expected_root_contract, ubi_blob)
    except root_binding_contract.RootBindingContractError as exc:
        raise BundleAuditError("combined UBI rootfs geometry differs from source contract") from exc
    expected_root_report = {**root_binding_contract.report_record(expected_root_contract,
        "dispatcher-include/bridge_root_binding_contract.generated.h"),
        "built_ubi": built_root_geometry}
    if report.get("root_binding_contract") != expected_root_report:
        raise BundleAuditError("bundle rootfs geometry report differs from independently parsed UBI")
    root_contract_path = bundle_root / "dispatcher-include/bridge_root_binding_contract.generated.h"
    try:
        root_header_bytes = root_contract_path.read_bytes()
        root_header_stat = root_contract_path.lstat()
    except OSError as exc:
        raise BundleAuditError("bundle rootfs geometry contract header is missing") from exc
    expected_root_header = root_binding_contract.render_header(expected_root_contract).encode("ascii")
    if (not stat.S_ISREG(root_header_stat.st_mode) or root_contract_path.is_symlink() or
            (root_header_stat.st_mode & 0o777) != 0o600 or root_header_bytes != expected_root_header or
            report["root_binding_contract"].get("sha256") != sha256(root_header_bytes)):
        raise BundleAuditError("bundle rootfs geometry header differs from independently derived source contract")
    kernel_report = _required(report, "kernel", dict, "report")
    stock_fit_rel = _safe_relpath(kernel_report.get("stock_fit_path"), "kernel.stock_fit_path")
    bridge_fit_rel = _safe_relpath(kernel_report.get("bridge_fit_path"), "kernel.bridge_fit_path")
    stock_fit_path = bundle_root.joinpath(*stock_fit_rel.parts)
    bridge_fit_path = bundle_root.joinpath(*bridge_fit_rel.parts)
    stock_kernel = stock_fit_path.read_bytes()
    bridge_kernel = bridge_fit_path.read_bytes()
    with tempfile.TemporaryDirectory(prefix="audit-fresh-combined-ubi-") as tmp:
        tmp = Path(tmp)
        bridge_ubi_path, stock_ubi_path = tmp / "bridge.ubi", tmp / "stock.ubi"
        bridge_ubi_path.write_bytes(ubi_blob)
        stock_ubi_path.write_bytes(stock_ubi_rows[0][1])
        try:
            bridge_meta, bridge_volumes, _, _ = vendor_ubi.parse(bridge_ubi_path)
            stock_meta, stock_volumes, _, _ = vendor_ubi.parse(stock_ubi_path)
        except Exception as exc:
            raise BundleAuditError("fresh combined or official UBI image rejected") from exc
        if bridge_meta["volume_table"] != stock_meta["volume_table"]:
            raise BundleAuditError("fresh combined UBI volume table differs from OEM")
        for name in set(stock_volumes) | set(bridge_volumes):
            if name in ("kernel", "ubi_rootfs"):
                continue
            if name not in stock_volumes or name not in bridge_volumes or bridge_volumes[name] != stock_volumes[name]:
                raise BundleAuditError("fresh bundle changed or omitted immutable OEM UBI volume: " + name)
        if bridge_volumes.get("rootfs_data"):
            raise BundleAuditError("fresh bundle unexpectedly prepopulates writable rootfs_data")
        if (stock_kernel != stock_volumes.get("kernel") or bridge_kernel != bridge_volumes.get("kernel") or
                kernel_report.get("stock_fit_sha256") != sha256(stock_kernel) or
                kernel_report.get("bridge_fit_sha256") != sha256(bridge_kernel) or
                kernel_report.get("default_preserved") is not True or
                kernel_report.get("all_configs_preserved") is not True):
            raise BundleAuditError("fresh shared FIT report pins differ from actual UBI contents")
        try:
            audit.audit_kernel(bridge_kernel, stock_kernel)
        except Exception as exc:
            raise BundleAuditError("fresh shared multi-config FIT preservation audit failed") from exc
        expected_routes = []
        unique_routes = {}
        for row in rows:
            route = row.get("route")
            try:
                facts = build.return_source.metadata(stock_kernel, bridge_kernel,
                    identity["sha256"], route=route)
            except Exception as exc:
                raise BundleAuditError("fresh shared FIT route/NAND-DT closure rejected") from exc
            if row.get("return_source") != facts:
                raise BundleAuditError("fresh provider route metadata differs from combined FIT closure")
            unique_routes[route] = facts
            expected_routes.append({"route": route, "compatible": facts["compatible"],
                                    "nand_dt_sha256": facts["nand_dt_sha256"]})
        if (kernel_report.get("routes") != expected_routes or
                kernel_report.get("compressed_kernel_sha256") != facts["compressed_kernel_sha256"]):
            raise BundleAuditError("fresh shared FIT route report differs from actual route closure")
        rootfs_report = _required(report, "rootfs", dict, "report")
        squash_path, squash, _ = _file(bundle_root, rootfs_report.get("squashfs_path"),
                                       "rootfs.squashfs_path")
        if (rootfs_report.get("bytes") != len(squash) or rootfs_report.get("sha256") != sha256(squash) or
                not _squashfs_volume_matches_image(bridge_volumes["ubi_rootfs"], squash)):
            raise BundleAuditError("fresh rootfs image differs from combined UBI bytes or fill")
        table_by_name = {entry["name"]: entry for entry in bridge_meta["volume_table"]}
        rootfs_volume = table_by_name.get("ubi_rootfs")
        stream_size = len(bridge_volumes["ubi_rootfs"])
        if (not isinstance(rootfs_volume, dict) or not 0 < len(squash) <= rootfs_volume.get("capacity", -1) or
                not 0 < stream_size <= rootfs_volume.get("capacity", -1)):
            raise BundleAuditError("fresh SquashFS/UBI rootfs exceeds OEM rootfs volume capacity")
        _audit_rootfs_files(bundle_root, report, bridge_volumes, rows, report.get("dispatcher"))
    return {"path": candidate_name, "bytes": len(candidate), "sha256": sha256(candidate),
            "ubi_sha256": sha256(ubi_blob), "rootfs_sha256": sha256(squash),
            "routes": expected_routes}


def audit_bundle(report_path, bundle_root, archive_path, payload_report_path,
                 archive_sha256=None, manifest_sha256=None, descriptors_sha256=None,
                 root_binding_gate_config=None):
    """Audit a finished bundle using explicit local inputs; returns a safe summary."""
    bundle_root = Path(bundle_root).resolve()
    report_path = Path(report_path)
    try:
        report_bytes = report_path.read_bytes()
        report = _json_unique(report_bytes.decode("utf-8"), "bundle report")
    except (OSError, BundleAuditError) as exc:
        raise BundleAuditError("cannot read bundle report") from exc
    except UnicodeError as exc:
        raise BundleAuditError("cannot decode bundle report") from exc
    workspace_root = Path(__file__).resolve().parents[3]
    if report.get("schema") == "bootbridge-provider-bundle-v2":
        result = _audit_fresh_candidate_bundle(report, bundle_root, archive_path,
            archive_sha256, manifest_sha256, descriptors_sha256, payload_report_path,
            root_binding_gate_config)
        result["build_report_sha256"] = sha256(report_bytes)
        try:
            final_report_bytes = report_path.read_bytes()
        except OSError as exc:
            raise BundleAuditError("bundle report disappeared during audit") from exc
        if final_report_bytes != report_bytes:
            raise BundleAuditError("bundle report changed during audit")
        return result
    candidate_path, candidate, official_path, official_bytes, identity = _extract_bundle(
        report, bundle_root, archive_path, workspace_root)
    allowed_classes = SUPPORTED_SOURCE_CLASSES.get(identity["sha256"])
    if allowed_classes is None:
        raise BundleAuditError("exact official source has no reviewed provider class set")
    required_classes = set(allowed_classes)
    rows = _required(report, "providers", list, "report")
    if {r.get("hardware_class") for r in rows if isinstance(r, dict)} != required_classes or len(rows) != len(required_classes):
        raise BundleAuditError("provider set does not exactly match this official product")
    try:
        parsed_archive = candidate_manifest.read_public_candidates(
            Path(archive_path), build.PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256)
    except Exception as exc:
        raise BundleAuditError("exact public candidate archive rejected") from exc
    if report.get("candidate_archive_sha256") != build.PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256:
        raise BundleAuditError("bundle is not bound to the exact reviewed public archive")
    provider_roots = {}
    for row in rows:
        if not isinstance(row, dict):
            raise BundleAuditError("provider row is not an object")
        board = row.get("hardware_class")
        profile = build.PUBLIC_BOARD_INPUTS.get(board)
        if board not in required_classes or not profile:
            raise BundleAuditError("unknown or wrong-product hardware class")
        desc = parsed_archive["boards"][board]
        revision_binding, revision_sha256 = _source_revision_binding(desc, board)
        if (row.get("source_revision_binding") != revision_binding or
                row.get("source_revision_sha256") != revision_sha256):
            raise BundleAuditError("provider candidate source-revision binding mismatch for HW-" + board)
        if row.get("record_id") != profile["record"] or row.get("product") != identity["model"]:
            raise BundleAuditError("provider record/product identity mismatch")
        payload_meta = row.get("payload")
        candidate_id = row.get("candidate_id")
        if candidate_id is None and isinstance(payload_meta, dict):
            candidate_id = payload_meta.get("candidate_id")
        if candidate_id != desc["candidate_id"]:
            raise BundleAuditError("provider candidate identity mismatch")
        payload_identity = payload_meta
        if (not isinstance(payload_identity, dict) or payload_identity.get("descriptor") != desc or
                payload_identity.get("mbn") != desc["files"]["appsbl.mbn"] or
                payload_identity.get("load") != desc["container"]["load"]):
            raise BundleAuditError("provider payload metadata differs from exact archive descriptor")
        provider_roots[board] = _audit_provider_source(row, board, identity, desc, bundle_root,
            official_path, archive_path, payload_report_path)

    # The final OEM wrapper and UBI are parsed independently from their bytes.
    try:
        candidate_identity, outer, images = audit.audit_wrapper(candidate, official_bytes)
    except Exception as exc:
        raise BundleAuditError("final bundle wrapper rejected") from exc
    if candidate_identity != identity:
        raise BundleAuditError("outer candidate identity changed")
    ubi_blob = outer[images[0]]["data"]
    stock_nodes = vendor_fit.fdt_props(official_bytes[160:160 + struct.unpack_from(">I", official_bytes, 164)[0]])
    stock_ubi_rows = _stock_ubi_rows(stock_nodes)
    if len(stock_ubi_rows) != 1:
        raise BundleAuditError("official source does not contain one exact stock UBI image")
    with tempfile.TemporaryDirectory(prefix="audit-provider-bundle-") as tmp:
        tmp = Path(tmp)
        bridge_ubi_path, stock_ubi_path = tmp / "candidate.ubi", tmp / "stock.ubi"
        bridge_ubi_path.write_bytes(ubi_blob)
        stock_ubi_path.write_bytes(stock_ubi_rows[0][1])
        bridge_meta, bridge_volumes, _, _ = vendor_ubi.parse(bridge_ubi_path)
        stock_meta, stock_volumes, _, _ = vendor_ubi.parse(stock_ubi_path)
        if bridge_meta["volume_table"] != stock_meta["volume_table"]:
            raise BundleAuditError("bundle UBI volume table differs from OEM")
        if any(bridge_volumes[n] != stock_volumes[n] for n in ("wifi_fw", "bt_fw")):
            raise BundleAuditError("bundle changed OEM ancillary firmware volumes")
        if bridge_volumes.get("rootfs_data"):
            raise BundleAuditError("bundle unexpectedly prepopulates writable rootfs_data")
        try:
            audit.audit_kernel(bridge_volumes["kernel"], stock_volumes["kernel"])
        except Exception as exc:
            raise BundleAuditError("shared multi-config FIT preservation audit failed") from exc
        kernel_report = _required(report, "kernel", dict, "report")
        _, stock_fit, _ = _file(bundle_root, kernel_report.get("stock_fit_path"), "kernel.stock_fit_path")
        _, bridge_fit, _ = _file(bundle_root, kernel_report.get("bridge_fit_path"), "kernel.bridge_fit_path")
        if (stock_fit != stock_volumes["kernel"] or bridge_fit != bridge_volumes["kernel"] or
                kernel_report.get("stock_fit_sha256") != sha256(stock_fit) or
                kernel_report.get("bridge_fit_sha256") != sha256(bridge_fit) or
                kernel_report.get("default_preserved") is not True or
                kernel_report.get("all_configs_preserved") is not True):
            raise BundleAuditError("shared FIT report pins differ from actual UBI contents")

        expected_records = []
        rootfs_report = _required(report, "rootfs", dict, "report")
        tree_rel = _safe_relpath(rootfs_report.get("tree_path"), "rootfs.tree_path")
        rootfs_tree = bundle_root.joinpath(*tree_rel.parts)
        for row in rows:
            board = row["hardware_class"]
            route = build.PUBLIC_BOARD_INPUTS[board]["route"]
            try:
                facts = build_return_source.metadata(stock_volumes["kernel"], bridge_volumes["kernel"],
                                                    identity["sha256"], route=route)
            except Exception as exc:
                raise BundleAuditError("actual shared FIT route/NAND closure rejected for HW-" + board) from exc
            _compare_profile_row(row, facts, board)
            nand = _required(row, "nand", dict, "provider")
            _compare_nand_tuple(nand, build.PUBLIC_BOARD_INPUTS[board], board)
            desc = parsed_archive["boards"][board]
            payload_member = desc["files"]["appsbl.partition.bin"]
            payload_row = _required(row, "payload", dict, "provider")
            payload_rel = _safe_relpath(payload_row.get("path", "").lstrip("/"), "payload.path")
            payload_path = rootfs_tree.joinpath(*payload_rel.parts)
            try:
                payload_st = payload_path.lstat()
            except OSError as exc:
                raise BundleAuditError("provider payload is missing from staged rootfs") from exc
            if not stat.S_ISREG(payload_st.st_mode) or payload_path.is_symlink():
                raise BundleAuditError("provider payload must be a regular file")
            payload = payload_path.read_bytes()
            if (len(payload) != payload_member["bytes"] or sha256(payload) != payload_member["sha256"] or
                    payload_row.get("sha256") != sha256(payload) or payload_row.get("bytes") != len(payload) or
                    payload_row.get("mode") != PAYLOAD_MODE or _mode(payload_st) != PAYLOAD_MODE):
                raise BundleAuditError("provider payload is not the exact archived board candidate")
            provider_row = _required(row, "provider", dict, "provider")
            provider_rel = _safe_relpath(provider_row.get("path", "").lstrip("/"), "provider.path")
            provider_path = rootfs_tree.joinpath(*provider_rel.parts)
            try:
                provider_st = provider_path.lstat()
            except OSError as exc:
                raise BundleAuditError("provider binary is missing from staged rootfs") from exc
            if not stat.S_ISREG(provider_st.st_mode) or provider_path.is_symlink():
                raise BundleAuditError("provider binary must be a regular file")
            provider = provider_path.read_bytes()
            provider_sha = _hex_digest(provider_row.get("sha256"), "provider.sha256")
            if (len(provider) != provider_row.get("bytes") or len(provider) <= 0 or
                    len(provider) > MAX_PROVIDER_BYTES or sha256(provider) != provider_sha or
                    provider_row.get("mode") not in PROVIDER_MODES or
                    _mode(provider_st) != provider_row.get("mode")):
                raise BundleAuditError("provider file size/hash/mode mismatch")
            expected_provider_path = "/usr/sbin/bridge-check-HW-" + board
            expected_payload_path = "/etc/bridge/providers/HW-" + board + "/uboot.bin"
            if provider_row.get("path") != expected_provider_path or payload_row.get("path") != expected_payload_path:
                raise BundleAuditError("provider or payload runtime path is not fixed for its board")
            source_root = provider_roots[board]
            source_provider = source_root / "rootfs" / Path(*provider_rel.parts)
            source_payload = source_root / "rootfs" / Path(*payload_rel.parts)
            if (not source_provider.is_file() or source_provider.is_symlink() or
                    source_provider.read_bytes() != provider or
                    not source_payload.is_file() or source_payload.is_symlink() or
                    source_payload.read_bytes() != payload):
                raise BundleAuditError("combined rootfs provider files differ from independently audited board builds")
            expected_records.append(_header_row(row, provider_row, payload_row))
        route_rows = [{"route": row["route"], "compatible": row["route_compatible"],
                       "nand_dt_sha256": row["nand_dt_sha256"]} for row in rows]
        if (kernel_report.get("routes") != route_rows or
                kernel_report.get("compressed_kernel_sha256") != facts["compressed_kernel_sha256"]):
            raise BundleAuditError("shared FIT selected-route report differs from actual route closure")

        dispatcher = _required(report, "dispatcher", dict, "report")
        dispatch_summary = _audit_dispatch_header(bundle_root, dispatcher, report.get("return_helper"),
                                                  expected_records, identity, rootfs_tree)
        _audit_rootfs_files(bundle_root, report, bridge_volumes, rows, dispatcher)

    return {
        "schema": "bootbridge-provider-bundle-audit-v1",
        "status": "PASS",
        "result": "PASS_OFFLINE_BUNDLE_AUDIT",
        "build_report_sha256": sha256(report_bytes),
        "source_identity": report["source_identity"],
        "supported_product_identities": report["supported_product_identities"],
        "official": {"model": identity["model"], "sha256": identity["sha256"], "bytes": len(official_bytes)},
        "candidate": {"sha256": sha256(candidate), "bytes": len(candidate)},
        "candidate_archive_sha256": build.PUBLIC_ABCD_CANDIDATE_ARCHIVE_SHA256,
        "providers": sorted(r["hardware_class"] for r in rows),
        "dispatcher": dispatch_summary,
        "hardware_validation": "NOT_EXECUTED",
    }


def _audit_provider_source(row, board, identity, descriptor, bundle_root, official_path,
                           archive_path, payload_report_path, archive_sha256=None,
                           manifest_sha256=None, descriptors_sha256=None):
    ref = _required(row, "provider_report", dict, "provider")
    report_path, raw, _ = _file(bundle_root, ref.get("path"), "provider_report.path")
    if ref.get("sha256") != sha256(raw):
        raise BundleAuditError("provider source BUILD-REPORT pin mismatch")
    try:
        source_report = _json_unique(raw, "provider source report")
    except BundleAuditError as exc:
        raise BundleAuditError("provider source BUILD-REPORT is invalid JSON") from exc
    expected_descriptor = row.get("payload", {}).get("descriptor", descriptor)
    if (source_report.get("source_official_sha256") != identity["sha256"] or
            source_report.get("hardware_class") != board or
            source_report.get("public_candidate_descriptor") != expected_descriptor or
            source_report.get("hardware_validation") != "NOT_EXECUTED"):
        raise BundleAuditError("provider source report is not bound to exact official/candidate inputs")
    candidate_meta = source_report.get("candidate")
    if not isinstance(candidate_meta, dict) or not isinstance(candidate_meta.get("name"), str):
        raise BundleAuditError("provider source candidate report is malformed")
    source_root = report_path.parent
    child_candidate, child_bytes, _ = _file(source_root, candidate_meta["name"], "provider candidate")
    if (candidate_meta.get("bytes") != len(child_bytes) or
            candidate_meta.get("sha256") != sha256(child_bytes)):
        raise BundleAuditError("provider source candidate differs from its build report")
    profile_ref = _required(row, "profile_header", dict, "provider")
    profile_path, profile_bytes, _ = _file(bundle_root, profile_ref.get("path"), "profile_header.path")
    if profile_ref.get("sha256") != sha256(profile_bytes):
        raise BundleAuditError("provider profile header pin mismatch")
    if not profile_bytes.startswith(b"#ifndef BOOTBRIDGE_FIRST_INSTALL_PROFILE_H\n"):
        raise BundleAuditError("provider profile header is not the expected generated source")
    # Re-run the mature independent per-provider auditor: it checks the exact
    # outer wrapper, archive descriptor, OEM boot-chain extraction, generated
    # headers, rootfs source markers, and ARM binaries against that report.
    try:
        audit.audit(child_candidate, official_path, payload_report_path, source_root,
                    candidate_archive=archive_path, hardware_class=board,
                    bundle_provider_paths=True,
                    fresh_candidate_descriptor=(source_report.get("public_candidate_descriptor")
                        if "appsbl.elf" in descriptor.get("files", {}) else None),
                    candidate_archive_sha256=archive_sha256,
                    candidate_manifest_sha256=manifest_sha256,
                    candidate_descriptors_sha256=descriptors_sha256)
    except Exception as exc:
        raise BundleAuditError("single-provider source audit failed for HW-" + board + ": " + str(exc)) from exc
    return source_root


def _rebuild_fresh_provider(source_root, row, board, identity, descriptor, official_bytes,
                            archive_path, archive_descriptors, payload_report_path,
                            matching_profiles, components, compiler, compiler_version):
    """Regenerate compile headers and byte-rebuild one staged ARM provider."""
    source_report_path = source_root / "BUILD-REPORT.json"
    source_report = _json_unique(source_report_path.read_bytes(), "provider source report")
    if source_report.get("public_candidate_descriptor") != row["payload"]["descriptor"]:
        raise BundleAuditError("provider rebuild descriptor differs from checked child report")
    compiler_path = Path(compiler).resolve(strict=True)
    try:
        actual_version = subprocess.run([str(compiler_path), "--version"], check=True,
            capture_output=True, text=True, timeout=15).stdout.splitlines()[0]
    except (OSError, subprocess.SubprocessError, IndexError) as exc:
        raise BundleAuditError("cannot verify provider rebuild compiler") from exc
    if actual_version != compiler_version:
        raise BundleAuditError("provider rebuild compiler version differs from dispatcher report")

    payload = candidate_manifest.extract_candidate_partition_v2(Path(archive_path), descriptor)
    base_data, patch_header, patch_metadata, storage = audit.fresh_payload_storage_contract(
        Path(archive_path), archive_descriptors, row["payload"]["descriptor"], payload)
    if (row["payload"].get("storage") != storage or
            patch_metadata.get("reconstructed_sha256") != row["payload"].get("sha256")):
        raise BundleAuditError("provider rebuild payload patch differs from the validated archive")

    if not matching_profiles:
        raise BundleAuditError("provider rebuild has no source-compatible NAND profile")
    bound = matching_profiles
    nand = bound[0]
    oob_mask = sum({64: 1, 128: 2, 256: 4}[p["mtd_oob_bytes"]] for p in bound)
    public_profile = dict(build.PUBLIC_BOARD_INPUTS[board], **{
        "class": board, "product_name": identity["model"], "source_sha256": identity["sha256"],
        "nand_profile_id": descriptor["nand_profile_id"],
        "nand_profile": dict(nand, accepted_mtd_oob_mask=oob_mask),
        "nand_profiles": bound, "accepted_mtd_oob_mask": oob_mask,
        "ecc": descriptor["storage_ecc_bits"], "load_ecc": descriptor["load_ecc_bits"],
        "candidate_id": descriptor["candidate_id"],
        "record": "hw-%s-%s-%s-%s" % (board.lower(), descriptor["nand_profile_id"],
            descriptor["load_ecc_bits"], descriptor["storage_ecc_bits"]),
        "model": nand["model_aliases"][0], "manufacturer": nand["manufacturer_id"],
        "chip": nand["chip_id"], "raw_spare": nand["raw_spare_bytes"],
        "mtd_oob": nand["mtd_oob_bytes"], "capacity": nand["capacity_mib"],
        "product_id": build.return_source.PRODUCTS[identity["sha256"]],
    })

    catalog_bytes = nand_schema.CATALOG.read_bytes()
    catalog_sha = sha256(catalog_bytes)
    nand_qualification = source_report.get("nand_qualification")
    payload_report = _json_unique(Path(payload_report_path).read_bytes(), "payload report")
    expected_profile_ids = [descriptor["nand_profile_id"]]
    if (payload_report.get("nand_catalog_sha256") != catalog_sha or
            not isinstance(nand_qualification, dict) or
            nand_qualification.get("catalog_sha256") != catalog_sha or
            nand_qualification.get("supported_nand_profile_ids") != expected_profile_ids or
            nand_qualification.get("appsbl_load_profiles") != expected_profile_ids or
            nand_qualification.get("appsbl_storage_profiles") != expected_profile_ids):
        raise BundleAuditError("provider rebuild catalog/profile binding differs from the source audit")
    catalog_binding = {"catalog_bytes": catalog_bytes, "catalog_sha256": catalog_sha,
        "supported": expected_profile_ids, "load": expected_profile_ids,
        "storage": expected_profile_ids}

    official_fit_len = struct.unpack_from(">I", official_bytes, 164)[0]
    official_fit = build.verify_fit(official_bytes[160:160 + official_fit_len])
    stock_ubi_images = [props["data"] for name, props in official_fit.items()
        if name.startswith("/images/ubi-") and name.count("/") == 2 and
        props.get("type") == b"firmware\0" and isinstance(props.get("data"), bytes)]
    if len(stock_ubi_images) != 1:
        raise BundleAuditError("provider rebuild cannot identify one exact official UBI input")
    root_contract = root_binding_contract.derive(identity["sha256"], stock_ubi_images[0])
    root_header = root_binding_contract.render_header(root_contract).encode("ascii")
    try:
        child_built_geometry = root_binding_contract.verify_same_source_and_built(
            root_contract, (source_root / "bridge.ubi").read_bytes())
    except (OSError, root_binding_contract.RootBindingContractError) as exc:
        raise BundleAuditError("provider child UBI fails exact rootfs geometry contract") from exc
    root_contract_record = {**root_binding_contract.report_record(root_contract),
                            "built_ubi": child_built_geometry}
    if source_report.get("root_binding_contract") != root_contract_record or row.get("root_binding_contract") != root_contract:
        raise BundleAuditError("provider child rootfs contract differs from exact official source")
    root_header_ref = row.get("root_binding_contract_header")
    provider_report_ref = row.get("provider_report", {})
    expected_header_path = (PurePosixPath(provider_report_ref.get("path", "")).parent /
                            "bridge_root_binding_contract.generated.h").as_posix()
    if (not isinstance(root_header_ref, dict) or
            root_header_ref.get("path") != expected_header_path or
            root_header_ref.get("sha256") != sha256(root_header) or
            root_header_ref.get("bytes") != len(root_header)):
        raise BundleAuditError("provider row rootfs contract header reference is invalid")
    child_header_path = source_root / "bridge_root_binding_contract.generated.h"
    if not child_header_path.is_file() or child_header_path.is_symlink() or child_header_path.read_bytes() != root_header:
        raise BundleAuditError("provider child rootfs contract header differs from official source geometry")

    source_metadata = source_report.get("return_source")
    if not isinstance(source_metadata, dict):
        raise BundleAuditError("provider rebuild return-source metadata is missing")
    source_header = build.return_source.c_header(source_metadata).encode("ascii")
    source_header_path = source_root / "bridge_oem_return_source.h"
    if not source_header_path.is_file() or source_header_path.read_bytes() != source_header:
        raise BundleAuditError("provider child return-source header differs from verified source facts")
    registry_info = source_report.get("return_source_registry")
    if not isinstance(registry_info, dict):
        raise BundleAuditError("provider rebuild return-source registry metadata is missing")
    source_variants = bundle_build.SOURCE_TABLE_VARIANTS[identity["sha256"]]
    source_table_mask = sum(nand_schema.TABLE_MASKS[v] for v in source_variants)
    if (registry_info.get("path") != "bridge_oem_return_registry.h" or
            registry_info.get("source_table_variants") != list(source_variants) or
            registry_info.get("source_table_mask") != source_table_mask or
            registry_info.get("exit_policy") != 2):
        raise BundleAuditError("provider child return-source registry metadata differs from reviewed source policy")
    registry_header = build.return_source.c_return_contract_header(
        [source_metadata], {identity["sha256"]: components}, nand_schema.profiles()["profiles"],
        source_table_mask=source_table_mask, exit_policy=2).encode("ascii")
    registry_path = source_root / "bridge_oem_return_registry.h"
    if (not registry_path.is_file() or registry_path.read_bytes() != registry_header or
            registry_info.get("sha256") != sha256(registry_header)):
        raise BundleAuditError("provider child return-source registry differs from verified source facts")

    with tempfile.TemporaryDirectory(prefix="audit-provider-rebuild-") as temp:
        work = Path(temp)
        include = work / "include"
        include.mkdir(mode=0o700)
        for name, data in (("bridge_oem_return_source.h", source_header),
                           ("bridge_oem_return_registry.h", registry_header),
                           ("h3c_nand_catalog.h", catalog_bytes),
                           ("bridge_root_binding_contract.generated.h", root_header)):
            (include / name).write_bytes(data)
        # Render the candidate ABI from the pinned archive ELF and descriptor.
        elf = candidate_manifest.extract_candidate_member_v2(
            Path(archive_path), descriptor, "appsbl.elf")
        bfi_header, bii_header = build.candidate_profile_codegen.render_headers(
            descriptor, elf, bound, descriptor["build_profile"], descriptor["bss_metadata"])
        (include / "bfi_candidate_profile.generated.h").write_text(bfi_header, encoding="ascii")
        (include / "bii_candidate_policy.generated.h").write_text(bii_header, encoding="ascii")
        first_header = build.generated_first_install_profile_header(board, identity, components,
            nand_profile=dict(nand, accepted_mtd_oob_mask=oob_mask),
            candidate_id=descriptor["candidate_id"])
        (include / "bridge_first_install_profile.h").write_text(first_header, encoding="ascii")
        (include / "bridge_payload_patch.h").write_text(patch_header, encoding="ascii")
        payload_header = build.generated_public_payload_header(
            build.PROJECT / "src/bridge_payload.h", payload, descriptor, catalog_binding,
            components, public_profile, row["payload"]["path"])
        (include / "bridge_payload.h").write_text(payload_header, encoding="ascii")
        # Recreate the OEM rule header from the pinned official source and exact
        # candidate; do not consume the child's transient .generated-include.
        build.generate_oem_header(work, include, stock_ubi_images[0], sha256(payload), identity,
            2, {"mtd_oob": nand["mtd_oob_bytes"], "ecc": nand["ecc_bits"]})
        output = work / "provider"
        command = _provider_stage_command(compiler_path, include, build.PROJECT / "src", output)
        try:
            subprocess.run(command, check=True, capture_output=True, timeout=120)
        except (OSError, subprocess.SubprocessError) as exc:
            detail = getattr(exc, "stderr", b"")
            if isinstance(detail, bytes):
                detail = detail.decode("utf-8", "replace")
            raise BundleAuditError("independent ARM provider rebuild failed: " + str(detail)) from exc
        rebuilt = output.read_bytes()

    provider_path = _safe_relpath(row["provider"]["path"].lstrip("/"), "provider.path")
    source_provider = source_root / "rootfs" / Path(*provider_path.parts)
    try:
        existing = source_provider.read_bytes()
    except OSError as exc:
        raise BundleAuditError("provider child ARM binary is missing from source rootfs") from exc
    if rebuilt != existing:
        raise BundleAuditError("ARM provider differs from independently rebuilt source/header bytes")
    return {"candidate_id": descriptor["candidate_id"], "hardware_class": board,
        "compiler_path": str(compiler_path), "compiler_version": actual_version,
        "source_order": list(PROVIDER_STAGE_SOURCES), "bytes": len(rebuilt),
        "sha256": sha256(rebuilt), "byte_exact": True}


def _compare_nand_tuple(actual, profile, board):
    expected = {
        "model": profile["model"], "manufacturer_id": profile["manufacturer"],
        "chip_id": profile["chip"], "capacity_mib": 128, "page_bytes": 2048,
        "erase_bytes": 131072, "raw_spare_bytes": profile["raw_spare"],
        "mtd_oob_bytes": profile["mtd_oob"], "ecc_bits": profile["ecc"],
        "jedec_bytes_observed": 2,
    }
    for key, value in expected.items():
        if actual.get(key) != value:
            raise BundleAuditError("HW-" + board + " NAND tuple mismatch: " + key)


def _stock_ubi_rows(fit_nodes):
    """Select UBI payload nodes only; other FIT firmware components are not UBI."""
    return [(path, props["data"]) for path, props in fit_nodes.items()
            if path.startswith("/images/ubi-") and path.count("/") == 2 and
            isinstance(props.get("data"), bytes) and props.get("type") == b"firmware\0"]


def _squashfs_volume_matches_image(volume, image):
    """Require the reported SquashFS exactly, followed only by erased UBI fill."""
    return (isinstance(volume, bytes) and isinstance(image, bytes) and
            len(image) <= len(volume) and volume[:len(image)] == image and
            volume[len(image):] == b"\xff" * (len(volume) - len(image)))


def _header_row(row, provider, payload):
    nand = row["nand"]
    storage = payload.get("storage", payload)
    return {
        "record_id": row["record_id"], "hardware_class": row["hardware_class"],
        "source_sha256": row["source_sha256"], "product": row["product"],
        "route": row["route"], "route_compatible": row["route_compatible"],
        "source_revision_sha256": row["source_revision_sha256"],
        "stock_kernel_sha256": row["stock_kernel_sha256"],
        "bridge_kernel_sha256": row["bridge_kernel_sha256"],
        "nand_dt_sha256": row["nand_dt_sha256"], "nand_model": nand["model"],
        "jedec_bytes_observed": nand["jedec_bytes_observed"],
        "manufacturer_id": nand["manufacturer_id"], "chip_id": nand["chip_id"],
        "capacity_mib": nand["capacity_mib"], "page_bytes": nand["page_bytes"],
        "erase_bytes": nand["erase_bytes"], "raw_spare_bytes": nand["raw_spare_bytes"],
        "mtd_oob_bytes": nand["mtd_oob_bytes"], "ecc_bits": nand["ecc_bits"],
        "provider_path": provider["path"], "provider_sha256": provider["sha256"],
        "provider_bytes": provider["bytes"], "payload_path": storage["path"],
        "payload_sha256": storage["sha256"], "payload_bytes": storage["bytes"],
    }


def _inventory_tree(tree, allow_special=False):
    result = {}
    for path in tree.rglob("*"):
        rel = path.relative_to(tree).as_posix()
        st = path.lstat()
        if stat.S_ISLNK(st.st_mode):
            entry = {"path": rel, "type": "symlink", "mode": _mode(st), "target": os.readlink(path)}
        elif stat.S_ISDIR(st.st_mode):
            entry = {"path": rel, "type": "directory", "mode": _mode(st)}
        elif stat.S_ISREG(st.st_mode):
            data = path.read_bytes()
            entry = {"path": rel, "type": "regular", "mode": _mode(st),
                     "bytes": len(data), "sha256": sha256(data)}
        elif allow_special and (stat.S_ISCHR(st.st_mode) or stat.S_ISBLK(st.st_mode)):
            entry = {"path": rel, "type": "char" if stat.S_ISCHR(st.st_mode) else "block",
                     "mode": _mode(st), "major": os.major(st.st_rdev), "minor": os.minor(st.st_rdev)}
        else:
            raise BundleAuditError("rootfs tree contains unsupported inode type")
        result[rel] = entry
    return result


def _parse_squashfs_listing(text):
    """Parse numeric unsquashfs -lln output, including one declared console node."""
    result = {}
    for line in text.splitlines():
        if not line.strip():
            continue
        # Staged paths are constrained elsewhere to ordinary Linux path names;
        # reject whitespace rather than ambiguously parsing a filename here.
        if not line.endswith("squashfs-root") and "squashfs-root/" not in line:
            raise BundleAuditError("SquashFS listing has an unexpected line")
        if line.endswith("squashfs-root"):
            prefix, rel = line[:-len("squashfs-root")], "."
        else:
            prefix, tail = line.split("squashfs-root/", 1)
            if " -> " in tail:
                rel, _ = tail.split(" -> ", 1)
            else:
                rel = tail
        if not rel or (rel != "." and any(char.isspace() for char in rel)):
            raise BundleAuditError("SquashFS listing contains an ambiguous path")
        fields = prefix.split()
        if len(fields) < 2 or fields[1] != "0/0":
            raise BundleAuditError("SquashFS member has unexpected numeric ownership")
        mode_string = fields[0]
        if len(mode_string) != 10:
            raise BundleAuditError("SquashFS listing has malformed mode bits")
        kind = {"-": "regular", "d": "directory", "l": "symlink",
                "c": "char", "b": "block", "p": "fifo", "s": "socket"}.get(mode_string[0])
        if kind is None:
            raise BundleAuditError("SquashFS listing has an unknown inode type")
        mode = 0
        for index, char in enumerate(mode_string[1:]):
            permission_index = index % 3
            shift = 6 - (index // 3) * 3
            if char == "r" and permission_index == 0:
                mode |= 4 << shift
            elif char == "w" and permission_index == 1:
                mode |= 2 << shift
            elif char == "x" and permission_index == 2:
                mode |= 1 << shift
            elif char in "sStT" and permission_index == 2:
                mode |= (0o4000, 0o2000, 0o1000)[index // 3]
                if char in "st":
                    mode |= 1 << shift
            elif char != "-":
                raise BundleAuditError("SquashFS listing has malformed permission bits")
        row = {"path": rel, "type": kind, "mode": mode}
        if kind == "char":
            if rel != "dev/console" or not re.fullmatch(
                    r"crw-------\s+0/0\s+5,\s+1\s+\d{4}-\d{2}-\d{2}\s+\d{2}:\d{2}\s+squashfs-root/dev/console",
                    line):
                raise BundleAuditError("SquashFS contains an undeclared character device")
            row.update(major=5, minor=1)
        if rel in result:
            raise BundleAuditError("SquashFS listing contains a duplicate path")
        result[rel] = row
    return result


def _audit_rootfs_files(bundle_root, report, bridge_volumes, rows, dispatcher):
    """Verify staged rootfs is the independently audited source rootfs plus exact files."""
    rootfs = _required(report, "rootfs", dict, "report")
    _, squash, _ = _file(bundle_root, rootfs.get("squashfs_path"), "rootfs.squashfs_path")
    if (not _squashfs_volume_matches_image(bridge_volumes["ubi_rootfs"], squash) or
            rootfs.get("bytes") != len(squash) or
            rootfs.get("sha256") != sha256(squash)):
        raise BundleAuditError("reported rootfs image differs from actual bundle UBI")
    tree_rel = _safe_relpath(rootfs.get("tree_path"), "rootfs.tree_path")
    tree = bundle_root.joinpath(*tree_rel.parts)
    if not tree.is_dir() or tree.is_symlink():
        raise BundleAuditError("staged rootfs tree is missing or unsafe")
    inventory = rootfs.get("file_inventory")
    if not isinstance(inventory, list):
        raise BundleAuditError("rootfs.file_inventory must enumerate the complete staged tree")
    recorded = {}
    for item in inventory:
        if not isinstance(item, dict) or not isinstance(item.get("path"), str):
            raise BundleAuditError("malformed rootfs inventory row")
        rel = _safe_relpath(item["path"], "rootfs inventory path").as_posix()
        if rel in recorded:
            raise BundleAuditError("duplicate rootfs inventory path")
        recorded[rel] = item
    actual = _inventory_tree(tree)
    normalized = {path: {k: v for k, v in item.items() if k not in ("origin", "official_source_record")}
                  for path, item in recorded.items()}
    if actual != normalized:
        raise BundleAuditError("staged rootfs tree differs from complete inventory")

    base_ref = _required(rootfs, "base_source_report", dict, "rootfs")
    base_report_path, raw, _ = _file(bundle_root, base_ref.get("path"), "rootfs.base_source_report.path")
    if base_ref.get("sha256") != sha256(raw):
        raise BundleAuditError("rootfs base source report hash mismatch")
    try:
        base_report = _json_unique(raw, "rootfs base source report")
    except BundleAuditError as exc:
        raise BundleAuditError("rootfs base source report is invalid JSON") from exc
    base_tree = base_report_path.parent / "rootfs"
    if not base_tree.is_dir() or base_tree.is_symlink():
        raise BundleAuditError("audited provider base rootfs tree is missing")
    base = _inventory_tree(base_tree)
    source_rows = base_report.get("rootfs", {}).get("oem_input_files", {})
    if not isinstance(source_rows, dict):
        raise BundleAuditError("base provider report has no exact OEM rootfs provenance index")
    for rel, item in recorded.items():
        if item.get("origin") == "official-curated":
            source_record = source_rows.get(rel)
            if source_record is None or item.get("official_source_record") != source_record:
                raise BundleAuditError("OEM-curated rootfs file lacks matching source provenance")
        elif "official_source_record" in item:
            raise BundleAuditError("non-OEM rootfs file is mislabeled with OEM provenance")
    generated = {"usr/sbin/bridge-check"}
    fresh_return_path = None
    if report.get("schema") == "bootbridge-provider-bundle-v2":
        helper = _required(report, "return_helper", dict, "return_helper")
        fresh_return_path = "/usr/sbin/bridge-oem-return"
        if (helper.get("path") != fresh_return_path or
                helper.get("binary_path") != "rootfs/usr/sbin/bridge-oem-return" or
                helper.get("mode") != 0o555):
            raise BundleAuditError("fresh return-helper rootfs identity/mode is invalid")
        helper_file, helper_bytes, helper_st = _file(bundle_root, helper.get("binary_path"),
                                                       "return_helper.binary_path")
        if (helper_bytes != (tree / fresh_return_path.lstrip("/")).read_bytes() or
                sha256(helper_bytes) != helper.get("sha256") or
                len(helper_bytes) != helper.get("bytes") or _mode(helper_st) != 0o555):
            raise BundleAuditError("fresh return-helper rootfs bytes/mode differ from immutable report pin")
        generated.add(fresh_return_path.lstrip("/"))
    for row in rows:
        generated.add(row["provider"]["path"].lstrip("/"))
        generated.add(row["payload"]["path"].lstrip("/"))
    generated_parents = {str(parent) for rel in generated for parent in PurePosixPath(rel).parents
                         if str(parent) != "."}
    for rel in generated_parents:
        if rel in actual and actual[rel]["type"] != "directory":
            raise BundleAuditError("generated provider path parent is not a directory")
        if rel in actual and actual[rel]["mode"] != 0o755:
            raise BundleAuditError("generated provider directory has unexpected permissions")
    for rel in set(base) | set(actual):
        if rel in generated or rel in generated_parents:
            continue
        if base.get(rel) != actual.get(rel):
            raise BundleAuditError("bundle changed or added an unapproved rootfs path")
    allowed_origins = {"official-curated", "bridge-runtime-generated", "compiled-dispatcher",
                       "compiled-provider", "public-candidate"}
    for rel, item in recorded.items():
        if item.get("origin") not in allowed_origins:
            raise BundleAuditError("rootfs inventory has unrecognized provenance")
        if rel in generated:
            expected_origin = ("compiled-dispatcher" if rel == "usr/sbin/bridge-check" else
                "bridge-runtime-generated" if rel == "usr/sbin/bridge-oem-return" and fresh_return_path else
                "compiled-provider" if any(rel == r["provider"]["path"].lstrip("/") for r in rows)
                else "public-candidate")
            if item["origin"] != expected_origin:
                raise BundleAuditError("generated rootfs file has unexpected provenance")

    pseudo_path, pseudo_bytes, _ = _file(bundle_root, rootfs.get("pseudo_path"), "rootfs.pseudo_path")
    if rootfs.get("pseudo_sha256") != sha256(pseudo_bytes) or pseudo_bytes != b"dev/console c 0600 0 0 5 1\n":
        raise BundleAuditError("rootfs pseudo-device list hash mismatch")
    # The pseudo list is passed to mksquashfs, so every special node in the
    # image must be declared there. WSL cannot mknod as an unprivileged user;
    # suppress only that known non-fatal extraction status, then verify the
    # numeric SquashFS listing and compare every extractable inode below.
    with tempfile.TemporaryDirectory(prefix="audit-squashfs-") as tmp:
        extract = Path(tmp) / "rootfs"
        image = str(bundle_root / rootfs["squashfs_path"])
        unsquashfs = os.environ.get("UNSQUASHFS", "unsquashfs")
        cp = subprocess.run([unsquashfs, "-no-progress", "-no-exit-code", "-d",
                             str(extract), image], capture_output=True, text=True, timeout=120)
        expected_warning = (f"create_inode: could not create character device {extract}/dev/console, "
                            "because you're not superuser!")
        warnings = [line for line in cp.stderr.splitlines() if line]
        if cp.returncode or warnings not in ([], [expected_warning]):
            raise BundleAuditError("cannot extract bundle SquashFS for independent file comparison")
        listing = subprocess.run([unsquashfs, "-lln", image], capture_output=True,
                                 text=True, timeout=120)
        if listing.returncode:
            raise BundleAuditError("cannot list bundle SquashFS for special-node verification")
        listed = _parse_squashfs_listing(listing.stdout)
        expected_listed = {
            path: {key: item[key] for key in ("path", "type", "mode")}
            for path, item in actual.items()
        }
        expected_listed["dev/console"] = {"path": "dev/console", "type": "char",
                                           "mode": 0o600, "major": 5, "minor": 1}
        expected_listed["."] = {"path": ".", "type": "directory",
                                 "mode": stat.S_IMODE(tree.stat().st_mode)}
        if listed != expected_listed:
            raise BundleAuditError("SquashFS listing differs from staged rootfs plus declared console")
        extracted = _inventory_tree(extract, allow_special=True)
    for rel, entry in actual.items():
        if extracted.get(rel) != entry:
            raise BundleAuditError("SquashFS content differs from staged rootfs tree")
    extra_nodes = {path: item for path, item in extracted.items() if path not in actual}
    expected_extra = {} if warnings else {"dev/console": {"path": "dev/console", "type": "char",
                                                            "mode": 0o600, "major": 5, "minor": 1}}
    if extra_nodes != expected_extra:
        raise BundleAuditError("SquashFS contains an undeclared special node or rootfs file")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--bundle-root", type=Path, required=True)
    parser.add_argument("--candidate-archive", type=Path, required=True)
    parser.add_argument("--candidate-archive-sha256")
    parser.add_argument("--candidate-manifest-sha256")
    parser.add_argument("--candidate-descriptors-sha256")
    parser.add_argument("--payload-report", type=Path, required=True,
                        help="pinned NAND catalog/payload report used by the source builds")
    parser.add_argument("--root-binding-gate-config", type=Path, required=True,
                        help="same pinned live QEMU GlueBI/sysfs gate inputs used for build")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    try:
        result = audit_bundle(args.report, args.bundle_root, args.candidate_archive, args.payload_report,
            args.candidate_archive_sha256, args.candidate_manifest_sha256,
            args.candidate_descriptors_sha256, args.root_binding_gate_config)
    except (BundleAuditError, OSError, ValueError, KeyError, AssertionError, subprocess.SubprocessError) as exc:
        parser.error("audit failed: " + str(exc))
    rendered = json.dumps(result, sort_keys=True, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")


if __name__ == "__main__":
    main()
