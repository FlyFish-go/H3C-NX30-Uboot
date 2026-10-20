#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact parser and source-bound compatibility schema for the OEM NAND catalog."""
import hashlib
import json
import re
from pathlib import Path

CATALOG = Path(__file__).resolve().parents[2] / "u-boot-2016/include/h3c_nand_catalog.h"
TABLE_MASKS = {"A0": 0x01, "A1": 0x02, "B": 0x04, "D": 0x08, "C": 0x10}
ECC_BITS = {4: 0x01, 8: 0x02}
OOB_BITS = {64: 0x01, 128: 0x02, 256: 0x04}


class NandSchemaError(ValueError):
    pass


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def _rows(text):
    start = text.index("static const struct h3c_nand_catalog_profile h3c_nand_catalog_profiles[] = {")
    end = text.index("\n};", start)
    rows = []
    for line in text[start:end].splitlines()[1:]:
        line = line.strip()
        if not line.startswith('{"'):
            continue
        vals, token, quoted, depth = [], "", False, 0
        for ch in line.rstrip(","):
            if ch == '"':
                quoted = not quoted
            if not quoted and ch == "{":
                depth += 1
            elif not quoted and ch == "}":
                depth -= 1
            if ch == "," and not quoted and depth == 1:
                vals.append(token.strip())
                token = ""
            else:
                token += ch
        vals.append(token.strip())
        vals = [value.strip().strip('"') for value in vals]
        if len(vals) != 24:
            raise NandSchemaError("catalog row field count changed")
        rows.append(vals)
    if len(rows) != 20:
        raise NandSchemaError("expected exact 20-entry audited NAND geometry table")
    return rows


def profiles(catalog_path=CATALOG):
    raw = Path(catalog_path).read_bytes()
    rows = _rows(raw.decode("ascii"))
    result = []
    for values in rows:
        profile_id, oem_model, qpic_model = values[:3]
        profile_id = profile_id.lstrip("{").strip('"')
        match = re.fullmatch(r"\{0x([0-9a-fA-F]+),0x([0-9a-fA-F]+)\}", values[3])
        if not match:
            raise NandSchemaError("catalog JEDEC tuple syntax changed")
        nums = [int(value.rstrip("} ").rstrip("Uu"), 0) for value in values[4:]]
        (capacity, qpic_capacity, page, pages_per_block, erase, appsbl_spare, appsbl_ecc,
         appsbl_sources, ecc4_sources, ecc8_sources, kernel_oob, kernel_ecc,
         kernel_step, kernel_rows, qpic_spare, qpic_ecc, runtime_ecc,
         qpic_oob4, qpic_oob8, caps) = nums
        if capacity not in (128 * 1024 * 1024, 256 * 1024 * 1024) or page != 2048 or erase != 131072:
            raise NandSchemaError("unsupported catalog geometry outside approved schema")
        result.append({
            "profile_id": profile_id, "oem_model": oem_model, "qpic_model": qpic_model,
            # Only the OEM model string is matched by the live bootloader
            # identity path. QPIC driver labels are diagnostic provenance and
            # are not aliases unless an exact OEM table row proves them.
            "model_aliases": [oem_model], "jedec_bytes_observed": 2,
            "manufacturer_id": int(match.group(1), 16), "chip_id": int(match.group(2), 16),
            "capacity_bytes": capacity, "capacity_mib": capacity // (1024 * 1024),
            "qpic_capacity_bytes": qpic_capacity,
            "qpic_capacity_mib": qpic_capacity // (1024 * 1024),
            "page_bytes": page, "pages_per_block": pages_per_block, "erase_bytes": erase,
            "raw_spare_bytes": appsbl_spare, "qpic_raw_spare_bytes": qpic_spare,
            "qpic_mtd_oob_ecc4_bytes": qpic_oob4, "qpic_mtd_oob_ecc8_bytes": qpic_oob8,
            "appsbl_ecc_mode_mask": appsbl_ecc, "appsbl_source_mask": appsbl_sources,
            "appsbl_ecc4_source_mask": ecc4_sources, "appsbl_ecc8_source_mask": ecc8_sources,
            "kernel_oem_oob_field_mask": kernel_oob, "kernel_ecc_mode_mask": kernel_ecc,
            "kernel_ecc_step_bytes": kernel_step, "kernel_image_rows": kernel_rows,
            "qpic_ecc_mode_mask": qpic_ecc, "runtime_ecc_mode_mask": runtime_ecc,
            "capabilities": caps,
            "source_table_mask": appsbl_sources,
        })
    ids = [row["profile_id"] for row in result]
    if len(set(ids)) != len(ids):
        raise NandSchemaError("duplicate catalog profile ID")
    return {"schema": "bootbridge-nand-profile-set-v1", "catalog_sha256": sha(raw),
            "profile_count": len(result), "profiles": result}


def profile_set_id(source_sha256, source_variants, catalog=None):
    if (not re.fullmatch(r"[0-9a-f]{64}", source_sha256) or not isinstance(source_variants, (list, tuple))
            or not source_variants or any(v not in TABLE_MASKS for v in source_variants)):
        raise NandSchemaError("exact source digest and OEM table variants required")
    catalog = catalog or profiles()
    mask = 0
    for variant in source_variants:
        mask |= TABLE_MASKS[variant]
    runtime_ids = {row["profile_id"] for row in runtime_profiles(source_variants, 0, catalog)}
    eligible = [row["profile_id"] for row in catalog["profiles"] if row["profile_id"] in runtime_ids]
    canonical = json.dumps({"source_sha256": source_sha256,
                            "catalog_sha256": catalog["catalog_sha256"],
                            "source_variants": list(source_variants), "profiles": eligible},
                           sort_keys=True, separators=(",", ":")).encode()
    return "nandset-" + source_sha256[:12] + "-" + sha(canonical)[:16], eligible


def runtime_profiles(source_variants, candidate_board_mask=0, catalog=None):
    """Expand exact OEM table/ECC/OOB masks into tuples; board eligibility is separate."""
    if not isinstance(source_variants, (list, tuple)) or not source_variants or any(v not in TABLE_MASKS for v in source_variants):
        raise NandSchemaError("explicit OEM source table variants required")
    if type(candidate_board_mask) is not int or candidate_board_mask < 0 or candidate_board_mask > 15:
        raise NandSchemaError("candidate board mask must be four class bits")
    catalog = catalog or profiles()
    result = []
    table_mask = 0
    for variant in source_variants:
        table_mask |= TABLE_MASKS[variant]
    for row in catalog["profiles"]:
        if not row["appsbl_source_mask"] & table_mask:
            continue
        for ecc, mode in ECC_BITS.items():
            ecc_source_mask = row["appsbl_ecc4_source_mask"] if ecc == 4 else row["appsbl_ecc8_source_mask"]
            if (not row["runtime_ecc_mode_mask"] & mode or not row["appsbl_ecc_mode_mask"] & mode or
                    not row["kernel_ecc_mode_mask"] & mode or
                    not table_mask & ecc_source_mask):
                continue
            for oob, oob_bit in OOB_BITS.items():
                if not row["kernel_oem_oob_field_mask"] & oob_bit:
                    continue
                result.append({
                    "profile_id": row["profile_id"], "model_aliases": row["model_aliases"],
                    "jedec_bytes_observed": row["jedec_bytes_observed"],
                    "manufacturer_id": row["manufacturer_id"], "chip_id": row["chip_id"],
                    "capacity_mib": row["capacity_mib"], "page_bytes": row["page_bytes"],
                    "erase_bytes": row["erase_bytes"], "raw_spare_bytes": row["raw_spare_bytes"],
                    "mtd_oob_bytes": oob, "ecc_bits": ecc,
                    "catalogue_eligible": candidate_board_mask,
                })
    return result


def candidate_set_id(source_sha256, archive_sha256, candidates):
    """Bind the complete candidate multiset; no candidate row can be swapped silently."""
    for digest in (source_sha256, archive_sha256):
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise NandSchemaError("candidate set requires exact source and archive SHA-256")
    if not isinstance(candidates, list) or not candidates:
        raise NandSchemaError("candidate set must be a nonempty list")
    fields = ("candidate_id", "board", "nand_profile_id", "load_ecc_bits", "storage_ecc_bits",
              "partition_sha256", "mbn_sha256", "load_sha256", "partition_bytes")
    normalized = []
    seen = set()
    for row in candidates:
        if not isinstance(row, dict) or any(key not in row for key in fields):
            raise NandSchemaError("candidate set row lacks exact candidate identity fields")
        if row["candidate_id"] in seen:
            raise NandSchemaError("duplicate candidate identity in candidate set")
        seen.add(row["candidate_id"])
        normalized.append({key: row[key] for key in fields})
    normalized.sort(key=lambda row: row["candidate_id"])
    canonical = json.dumps({"schema": "bootbridge-candidate-set-v1",
                            "source_sha256": source_sha256,
                            "archive_sha256": archive_sha256,
                            "candidates": normalized}, sort_keys=True,
                           separators=(",", ":")).encode()
    return "candset-" + sha(canonical)[:16]


def write_json(path, catalog=None):
    doc = catalog or profiles()
    Path(path).write_text(json.dumps(doc, sort_keys=True, indent=2) + "\n")
    return doc


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    write_json(args.output)
