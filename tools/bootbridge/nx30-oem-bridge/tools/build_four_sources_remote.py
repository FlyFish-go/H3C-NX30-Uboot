#!/usr/bin/env python3
"""Run the four source-bound v2 bundle builds and independent audits in WSL.

Input manifest schema 1 is intentionally explicit. Each source row pins its
original BIN and extracted OEM rootfs tree; the manifest also pins the shared
payload report and build tools. The runner never guesses paths from a checkout.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
import stat
from pathlib import Path
import platform
import subprocess
import sys

PRODUCTS = ("RC3000", "RT3000", "RW3000", "NX30")
LOGICAL_PRODUCTS = {
    "RC3000": ("RC3000", ("A", "B", "C", "D")),
    "RT3000": ("RT3000", ("A", "B", "C", "D")),
    "RW3000": ("RW3000", ("A", "B", "C", "D")),
    "NX30": ("NX30", ("D",)),
    "RX3000": ("NX30", ("A", "B", "C", "D")),
}
RX_EVIDENCE = Path(__file__).resolve().parents[1] / "tools/fixtures/RX-OTA-SOURCE-EVIDENCE.json"
RX_EVIDENCE_SHA256 = "9834e1b2b92b59d4822905fc5ebe9096462e44106534b440598c85a67c2bf4c6"
RX_EVIDENCE_BYTES = 13544
EXPECTED = {
    "RC3000": ("69d3e558e770f4dc26f0ff9ddd6a5a10f541b3fdc826aacf3cbeaf1f93469c44", "RC3000V100R013L01"),
    "RT3000": ("cb2966e54ae1e143c6c7dd55acdb6ed22bb011a0e421c8860d5724c419d8f28b", "RT3000V100R015"),
    "RW3000": ("5d2d8c62dd244db7dbb4b5a4f0430453b411b886ec70445903e3d0b578766fb5", "RW3000V100R012"),
    "NX30": ("61fbc2a356aa9d5d73eaa9f02eb87f455adea8c32684ffb627b2a35afc2ce922", "NX30V100R012"),
}
# Populated by the caller: no private absolute paths belong in published source.
INPUT_DIR = Path(os.environ.get("BOOTBRIDGE_OFFICIAL_INPUTS", "/path/to/official-firmware"))
ROOTFS_DIR = Path(os.environ.get("BOOTBRIDGE_ROOTFS_INPUTS", "/path/to/extracted-rootfs"))
OFFICIAL_PATHS = {product: INPUT_DIR / f"{product}-original.bin" for product in PRODUCTS}
ROOTFS_PATHS = {product: ROOTFS_DIR / product / "rootfs" for product in PRODUCTS}
HEX = set("0123456789abcdef")


def sha_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def fail(message: str) -> None:
    raise ValueError(message)


def pinned_file(row: dict, key: str) -> Path:
    item = row.get(key)
    if not isinstance(item, dict) or set(item) != {"path", "sha256", "bytes"}:
        fail(f"{key} must contain only path, sha256, bytes")
    path = Path(item["path"])
    if not path.is_absolute() or not path.is_file() or path.is_symlink():
        fail(f"{key} is not an absolute regular file: {path}")
    digest = item["sha256"]
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in HEX for c in digest):
        fail(f"{key} SHA-256 is malformed")
    if path.stat().st_size != item["bytes"] or sha_file(path) != digest:
        fail(f"{key} does not match its supplied byte/hash pin: {path}")
    return path.resolve(strict=True)


def pinned_tree(row: dict, key: str) -> Path:
    item = row.get(key)
    if not isinstance(item, dict) or set(item) != {"path", "sha256"}:
        fail(f"{key} must contain only path and sha256")
    root = Path(item["path"])
    if not root.is_absolute() or not root.is_dir() or root.is_symlink():
        fail(f"{key} is not an absolute directory: {root}")
    # Match the independently recorded rootfs-tree.json contract: regular files
    # bind path/hash/bytes/mode, symlinks bind path and literal target.
    rows = []
    for path in sorted(root.rglob("*")):
        st = path.lstat()
        if stat.S_ISLNK(st.st_mode):
            rows.append({"path": path.relative_to(root).as_posix(),
                         "symlink": os.readlink(path)})
        elif stat.S_ISREG(st.st_mode):
            rows.append({"path": path.relative_to(root).as_posix(),
                         "sha256": sha_file(path), "bytes": st.st_size,
                         "mode": st.st_mode & 0o777})
        elif stat.S_ISDIR(st.st_mode):
            continue
        else:
            fail(f"unsupported inode in {key}: {path}")
    raw = json.dumps(rows, sort_keys=True, separators=(",", ":")).encode()
    digest = hashlib.sha256(raw).hexdigest()
    if item["sha256"] != digest:
        fail(f"{key} canonical tree digest mismatch: {root}")
    return root.resolve(strict=True)


def load_manifest(path: Path):
    if path.is_symlink() or not path.is_file():
        fail("input manifest must be a regular file")
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schema") != 1 or set(data) != {
            "schema", "sources", "payload_report", "tools"}:
        fail("input manifest must use the documented schema 1 keys")
    sources = data["sources"]
    if not isinstance(sources, list) or [x.get("product") for x in sources] != list(PRODUCTS):
        fail("manifest must contain the four sources in canonical RC, RT, RW, NX order")
    resolved = []
    for row in sources:
        product = row["product"]
        if set(row) != {"product", "official", "rootfs"}:
            fail(f"{product} source row has unexpected fields")
        official = pinned_file(row, "official")
        rootfs = pinned_tree(row, "rootfs")
        expected_sha, expected_identity = EXPECTED[product]
        if (official != OFFICIAL_PATHS[product].resolve(strict=True) or
                rootfs != ROOTFS_PATHS[product].resolve(strict=True)):
            fail(f"{product} input paths differ from the reviewed original/rootfs paths")
        if row["official"]["sha256"] != expected_sha:
            fail(f"{product} original BIN is not the reviewed exact source")
        resolved.append({"product": product, "official": official, "rootfs": rootfs,
                         "official_pin": row["official"], "rootfs_pin": row["rootfs"],
                         "expected_identity": expected_identity})
    payload = pinned_file(data, "payload_report")
    tools = data["tools"]
    if not isinstance(tools, dict) or set(tools) != {"cc", "ubinize", "mksquashfs", "dtc"}:
        fail("tools must pin cc, ubinize, mksquashfs, and dtc")
    resolved_tools = {}
    for name, raw in tools.items():
        if not isinstance(raw, dict) or set(raw) != {"path", "sha256"}:
            fail(f"tool {name} must contain only path and sha256")
        tool = Path(raw["path"])
        if not tool.is_absolute() or not tool.is_file() or tool.is_symlink() or not os.access(tool, os.X_OK):
            fail(f"tool {name} is not an executable absolute file")
        if sha_file(tool) != raw["sha256"]:
            fail(f"tool {name} digest mismatch")
        resolved_tools[name] = tool.resolve(strict=True)
    return data, resolved, payload, resolved_tools


def write_json(path: Path, value: dict) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + ".tmp")
    temp.write_text(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")
    temp.replace(path)


def report_ref(path: Path) -> dict:
    raw = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(raw),
            "sha256": hashlib.sha256(raw).hexdigest()}


def logical_target_matrix():
    return [(model, board) for model, (_, boards) in LOGICAL_PRODUCTS.items()
            for board in boards]


def logical_coverage_groups(source_model: str):
    return [(model, boards) for model, (source, boards) in LOGICAL_PRODUCTS.items()
            if source == source_model]


def logical_receipt(out: Path, model: str, source_row: dict, report: dict,
                    report_path: Path, audit_path: Path, boards: tuple[str, ...],
                    rx_evidence: dict | None) -> dict:
    provider_rows = [row for row in report.get("providers", [])
                     if isinstance(row, dict) and row.get("hardware_class") in boards]
    actual_boards = sorted({row["hardware_class"] for row in provider_rows})
    if actual_boards != list(boards):
        fail(f"{model} lacks the exact audited provider class set")
    candidate_ids = sorted({row["candidate_id"] for row in provider_rows
                            if isinstance(row.get("candidate_id"), str)})
    if not candidate_ids:
        fail(f"{model} has no exact candidate IDs in its provider rows")
    dispatcher_sha256 = report.get("dispatcher", {}).get("sha256")
    if not isinstance(dispatcher_sha256, str) or len(dispatcher_sha256) != 64:
        fail(f"{model} lacks the audited dispatcher digest")
    candidate = report["candidate"]
    receipt = {"schema": "bootbridge-logical-coverage-receipt-v1",
        "status": "PASS_LOGICAL_COVERAGE_PROVENANCE",
        "software_contract_status": "SOFTWARE_CONTRACT_ONLY",
        "bundle_id": source_row["bundle_id"], "source_model": report["official"]["model"],
        "source_version": report["official"]["version"],
        "source_sha256": report["official"]["sha256"],
        "candidate_bytes": candidate["bytes"], "candidate_sha256": candidate["sha256"],
        "model": model, "providers": list(boards),
        "candidate_ids": candidate_ids, "provider_records": len(candidate_ids),
        "build_report_sha256": report_ref(report_path)["sha256"],
        "audit_report_sha256": report_ref(audit_path)["sha256"],
        "dispatcher_sha256": dispatcher_sha256,
        "hardware_validation": "NOT_EXECUTED",
        "physical_install_qualification": "NOT_GRANTED"}
    if rx_evidence is not None:
        receipt["identity_evidence"] = rx_evidence
    path = out / "logical-coverage" / f"{model}.json"
    write_json(path, receipt)
    return {"model": model, "providers": list(boards), "evidence": report_ref(path)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-manifest", required=True, type=Path)
    parser.add_argument("--candidate-archive", required=True, type=Path)
    parser.add_argument("--candidate-archive-sha256", required=True)
    parser.add_argument("--candidate-manifest-sha256", required=True)
    parser.add_argument("--candidate-descriptors-sha256", required=True)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--jobs", type=int, choices=range(1, 5), default=1,
                        help="number of independent source build/audit subprocess pairs (default: 1)")
    parser.add_argument("--bridge-source", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--root-binding-gate-config", required=True, type=Path,
                        help="pinned isolated QEMU/nandsim gate configuration")
    parser.add_argument("--release-set", default="nand-oem-repair-20261017",
                        help="explicit internal release identity recorded in the audited result")
    args = parser.parse_args(argv)
    if (args.root_binding_gate_config.is_symlink() or
            not args.root_binding_gate_config.is_file()):
        parser.error("root binding gate configuration must be a regular file")
    if (not args.release_set or len(args.release_set) > 96 or
            any(c not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_."
                for c in args.release_set)):
        parser.error("release-set must be a bounded ASCII identifier")
    if platform.system() != "Linux" or "microsoft" not in platform.release().lower():
        parser.error("this runner is WSL-only; it does not dispatch remote/Windows jobs")
    for pin in (args.candidate_archive_sha256, args.candidate_manifest_sha256,
                args.candidate_descriptors_sha256):
        if len(pin) != 64 or any(c not in HEX for c in pin):
            parser.error("all three candidate archive SHA-256 pins are required")
    if args.candidate_archive.is_symlink() or not args.candidate_archive.is_file():
        parser.error("candidate archive must be a regular file")
    if sha_file(args.candidate_archive) != args.candidate_archive_sha256:
        parser.error("candidate archive does not match its explicit SHA-256 pin")
    source = args.bridge_source.resolve(strict=True)
    if not (source / "tools/build_bundle.py").is_file() or not (source / "tools/audit_bundle.py").is_file():
        parser.error("bridge source must contain build_bundle.py and audit_bundle.py")
    if "RT3000" not in str(source):
        parser.error("bridge source must be the WSL project checkout")
    out = args.output_root
    if not out.is_absolute() or out.exists() or out.is_symlink():
        parser.error("output root must be a new absolute directory")

    try:
        manifest, rows, payload, tools = load_manifest(args.input_manifest.resolve(strict=True))
    except (OSError, ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        parser.error(f"input manifest validation failed: {exc}")
    rx_bytes = RX_EVIDENCE.read_bytes()
    if len(rx_bytes) != RX_EVIDENCE_BYTES or hashlib.sha256(rx_bytes).hexdigest() != RX_EVIDENCE_SHA256:
        parser.error("checked-in RX3000 shared-source identity evidence failed its exact pin")
    out.mkdir(mode=0o700, parents=True)
    summary = {"schema": 1, "status": "RUNNING", "release_set": args.release_set,
               "input_manifest": str(args.input_manifest.resolve()),
               "input_manifest_sha256": sha_file(args.input_manifest.resolve()),
               "root_binding_gate_config": report_ref(args.root_binding_gate_config),
               "payload_report": {"path": str(payload), "sha256": sha_file(payload),
                                  "bytes": payload.stat().st_size},
               "tools": {name: {"path": str(path), "sha256": sha_file(path)}
                         for name, path in tools.items()},
               "candidate_archive": {"path": str(args.candidate_archive.resolve()),
                   "sha256": args.candidate_archive_sha256,
                   "manifest_sha256": args.candidate_manifest_sha256,
                   "descriptors_sha256": args.candidate_descriptors_sha256},
               "sources": [], "note": "NX30/RX3000 shared source retains A-D package provider coverage; product identity gates remain source-bound."}
    (out / "RUN-SUMMARY.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    builder = source / "tools/build_bundle.py"
    auditor = source / "tools/audit_bundle.py"
    result_bundles = []
    def build_one(row):
        product = row["product"]
        bundle_id = product + "-" + row["official_pin"]["sha256"][:12]
        product_out = out / product
        report = product_out / "BUILD-REPORT.json"
        audit = product_out / "AUDIT.json"
        command = [sys.executable, str(builder), "--official", str(row["official"]),
                   "--rootfs", str(row["rootfs"]), "--output", str(product_out),
                   "--cc", str(tools["cc"]), "--ubinize", str(tools["ubinize"]),
                   "--mksquashfs", str(tools["mksquashfs"]), "--dtc", str(tools["dtc"]),
                   "--candidate-archive", str(args.candidate_archive.resolve()),
                   "--candidate-archive-sha256", args.candidate_archive_sha256,
                   "--candidate-manifest-sha256", args.candidate_manifest_sha256,
                   "--candidate-descriptors-sha256", args.candidate_descriptors_sha256,
                   "--payload-report", str(payload),
                   "--root-binding-gate-config", str(args.root_binding_gate_config.resolve())]
        def run_logged(argv, log_path):
            cp = subprocess.run(argv, cwd=source, capture_output=True)
            log_path.write_bytes(cp.stdout + (b"\n--- STDERR ---\n" if cp.stderr else b"") + cp.stderr)
            if cp.returncode:
                fail(f"{product} subprocess failed ({cp.returncode}); see {log_path}")
        run_logged(command, out / (product + "-build.log"))
        report_doc = json.loads(report.read_text(encoding="utf-8"))
        if (report_doc.get("official", {}).get("sha256") != row["official_pin"]["sha256"] or
                report_doc.get("official", {}).get("identity") != row["expected_identity"]):
            fail(f"{product} output report source hash mismatch")
        audit_command = [sys.executable, str(auditor), "--report", str(report),
                         "--bundle-root", str(product_out), "--candidate-archive",
                         str(args.candidate_archive.resolve()), "--candidate-archive-sha256",
                         args.candidate_archive_sha256, "--candidate-manifest-sha256",
                         args.candidate_manifest_sha256, "--candidate-descriptors-sha256",
                         args.candidate_descriptors_sha256, "--payload-report", str(payload),
                         "--output", str(audit),
                         "--root-binding-gate-config", str(args.root_binding_gate_config.resolve())]
        run_logged(audit_command, out / (product + "-audit.log"))
        audit_doc = json.loads(audit.read_text(encoding="utf-8"))
        if audit_doc.get("status") != "PASS":
            fail(f"{product} independent audit did not report PASS")
        logical_rows = []
        for logical_model, boards in logical_coverage_groups(product):
            evidence = ({"path": str(RX_EVIDENCE.resolve()), "bytes": len(rx_bytes),
                         "sha256": RX_EVIDENCE_SHA256}
                        if product == "NX30" and logical_model == "RX3000" else None)
            logical_rows.append(logical_receipt(out, logical_model,
                dict(row, bundle_id=bundle_id), report_doc, report, audit, boards, evidence))
        candidate_path = product_out / report_doc["candidate"]["path"]
        if (candidate_path.stat().st_size != report_doc["candidate"]["bytes"] or
                sha_file(candidate_path) != report_doc["candidate"]["sha256"]):
            fail(f"{product} combined candidate output differs from audited report")
        rootfs_doc = report_doc.get("rootfs", {})
        bundle = {"bundle_id": bundle_id,
            "official": {"path": str(row["official"]), "identity": report_doc["official"]["identity"],
                "model": report_doc["official"]["model"], "version": report_doc["official"]["version"],
                "bytes": report_doc["official"]["bytes"], "sha256": report_doc["official"]["sha256"]},
            "candidate": {"path": str(candidate_path.resolve()),
                "bytes": report_doc["candidate"]["bytes"], "sha256": report_doc["candidate"]["sha256"]},
            "build_report": report_ref(report), "audit_report": report_ref(audit),
            "logical_coverage": logical_rows,
            "dispatcher": {"sha256": report_doc["dispatcher"]["sha256"],
                "records_count": report_doc["dispatcher"]["records_count"]}}
        source_summary = {"product": product, "official": row["official_pin"],
            "rootfs": row["rootfs_pin"], "output": str(product_out),
            "bundle_sha256": report_doc.get("bundle_sha256"),
            "compressed_rootfs_bytes": rootfs_doc.get("bytes"), "audit_status": audit_doc["status"]}
        return bundle, source_summary

    try:
        completed = {}
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(build_one, row): row["product"] for row in rows}
            for future in as_completed(futures):
                product = futures[future]
                completed[product] = future.result()
        for row in rows:
            bundle, source_summary = completed[row["product"]]
            result_bundles.append(bundle)
            summary["sources"].append(source_summary)
        summary["status"] = "FOUR_SOURCE_BUILD_AND_AUDIT_PASS"
    except Exception as exc:
        summary["status"] = "FAILED"
        summary["failure"] = f"{type(exc).__name__}: {exc}"
        (out / "RUN-SUMMARY.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
        raise
    (out / "RUN-SUMMARY.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    result = {"schema": "bootbridge-provider-bundle-build-result-v1", "status": "PASS",
        "release_set": args.release_set, "engine_revision": 2,
        "candidate_archive": {"path": str(args.candidate_archive.resolve()),
            "bytes": args.candidate_archive.stat().st_size,
            "sha256": args.candidate_archive_sha256,
            "manifest_sha256": args.candidate_manifest_sha256,
            "descriptors_sha256": args.candidate_descriptors_sha256},
        "bundles": result_bundles}
    coverage = [entry for bundle in result_bundles for entry in bundle["logical_coverage"]]
    if (len(result_bundles) != 4 or len(coverage) != 5 or
            sum(len(entry["providers"]) for entry in coverage) != 17 or
            len({entry["model"] for entry in coverage}) != 5):
        fail("audited result does not cover the exact four-source/17-logical target matrix")
    write_json(out / "RESULT.json", result)
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
