#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Generate a deterministic compile-time delta from one pinned APPSBL to another.

This is a pure byte transformer. It does not read archives, discover hardware,
or authorize installation. Callers must obtain descriptors and payload bytes
from already verified archive members.
"""
from __future__ import annotations

import hashlib
import json
import re
from typing import Any

APPSBL_BYTES = 0x140000
BASE_PROFILE_ID = "h3c-oem-efba-128m-2k-sp64"


class PayloadPatchError(ValueError):
    pass


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _digest(value: Any, label: str) -> str:
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise PayloadPatchError(f"{label} must be a lowercase SHA-256")
    return value


def _descriptor_binding(descriptor: dict[str, Any], payload: bytes, label: str) -> tuple[str, str]:
    if not isinstance(descriptor, dict):
        raise PayloadPatchError(f"{label} descriptor must be an object")
    files = descriptor.get("files")
    part = files.get("appsbl.partition.bin") if isinstance(files, dict) else None
    if not isinstance(part, dict):
        raise PayloadPatchError(f"{label} descriptor lacks appsbl.partition.bin")
    if type(part.get("bytes")) is not int or part["bytes"] != APPSBL_BYTES or len(payload) != APPSBL_BYTES:
        raise PayloadPatchError(f"{label} APPSBL partition must be exactly 0x140000 bytes")
    digest = _digest(part.get("sha256"), f"{label} partition SHA")
    if _sha(payload) != digest:
        raise PayloadPatchError(f"{label} payload bytes do not match the pinned descriptor")
    candidate_id = descriptor.get("candidate_id")
    if not isinstance(candidate_id, str) or not re.fullmatch(r"[A-Za-z0-9._:-]{1,160}", candidate_id):
        raise PayloadPatchError(f"{label} candidate ID is malformed")
    try:
        canonical = json.dumps(descriptor, sort_keys=True, separators=(",", ":"),
                               ensure_ascii=True).encode("ascii")
    except (TypeError, ValueError, UnicodeEncodeError) as exc:
        raise PayloadPatchError(f"{label} descriptor is not canonical JSON data") from exc
    return candidate_id, _sha(canonical)


def _cstr(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def _diff_ranges(base: bytes, target: bytes, merge_gap: int) -> list[tuple[int, int]]:
    ranges: list[tuple[int, int]] = []
    at = 0
    current_start = -1
    current_end = -1
    size = len(base)
    while at < size:
        if base[at] == target[at]:
            at += 1
            continue
        start = at
        at += 1
        while at < size and base[at] != target[at]:
            at += 1
        end = at
        if current_start >= 0 and start - current_end <= merge_gap:
            current_end = end
        else:
            if current_start >= 0:
                ranges.append((current_start, current_end))
            current_start, current_end = start, end
    if current_start >= 0:
        ranges.append((current_start, current_end))
    return ranges


def generate_patch_header(base_descriptor: dict[str, Any], base_bytes: bytes,
                          candidate_descriptor: dict[str, Any], candidate_bytes: bytes,
                          merge_gap: int = 8) -> tuple[str, dict[str, Any]]:
    """Return a C header and audit metadata for exact base-to-candidate bytes.

    Ranges are sorted and disjoint. Consecutive changed ranges are coalesced
    when the unchanged gap is at most ``merge_gap`` bytes; those gap bytes are
    included from the exact target image. Both descriptors must identify the
    same board, source snapshot, and load/storage ECC pair.
    """
    if type(merge_gap) is not int or not 0 <= merge_gap <= 64:
        raise PayloadPatchError("merge_gap must be an integer from 0 through 64")
    base_id, base_descriptor_sha = _descriptor_binding(base_descriptor, base_bytes, "base")
    candidate_id, candidate_descriptor_sha = _descriptor_binding(
        candidate_descriptor, candidate_bytes, "candidate")
    for key in ("board", "source_snapshot_sha256"):
        if base_descriptor.get(key) != candidate_descriptor.get(key):
            raise PayloadPatchError(f"base and candidate differ in required identity field {key}")
    board = base_descriptor.get("board")
    if not isinstance(board, str) or len(board) != 1 or board not in "ABCD":
        raise PayloadPatchError("descriptor board class is invalid")
    if (base_descriptor.get("nand_profile_id") != BASE_PROFILE_ID or
            base_descriptor.get("load_ecc_bits") != 4 or
            base_descriptor.get("storage_ecc_bits") != 4):
        raise PayloadPatchError("patch base must be the exact EF:BA load4/store4 source candidate")
    source_sha = _digest(base_descriptor.get("source_snapshot_sha256"), "source snapshot SHA")
    if source_sha != _digest(candidate_descriptor.get("source_snapshot_sha256"),
                             "candidate source snapshot SHA"):
        raise PayloadPatchError("candidate source snapshot SHA is malformed or differs")
    for label, descriptor in (("base", base_descriptor), ("candidate", candidate_descriptor)):
        if (descriptor.get("load_ecc_bits") not in (4, 8) or
                descriptor.get("storage_ecc_bits") not in (4, 8) or
                descriptor["load_ecc_bits"] != descriptor["storage_ecc_bits"]):
            raise PayloadPatchError(f"{label} source candidate load/storage ECC contract is invalid")

    ranges = _diff_ranges(base_bytes, candidate_bytes, merge_gap)
    patches: list[tuple[int, int, int]] = []
    patch_data = bytearray()
    for start, end in ranges:
        count = end - start
        data_offset = len(patch_data)
        if (start < 0 or count <= 0 or start > APPSBL_BYTES or count > APPSBL_BYTES-start or
                data_offset > APPSBL_BYTES or count > APPSBL_BYTES-data_offset):
            raise PayloadPatchError("patch range or aggregate data exceeds APPSBL bounds")
        patches.append((start, count, data_offset))
        patch_data.extend(candidate_bytes[start:end])
    rebuilt = bytearray(base_bytes)
    previous_end = 0
    expected_data_offset = 0
    for offset, count, data_offset in patches:
        if (count == 0 or offset + count > len(rebuilt) or
                data_offset + count > len(patch_data) or offset < previous_end or
                data_offset != expected_data_offset):
            raise PayloadPatchError("generated patch table failed bounds validation")
        rebuilt[offset:offset+count] = patch_data[data_offset:data_offset+count]
        previous_end = offset + count
        expected_data_offset = data_offset + count
    if bytes(rebuilt) != candidate_bytes:
        raise PayloadPatchError("generated patch does not reconstruct exact candidate bytes")
    base_sha = _sha(base_bytes)
    final_sha = _sha(candidate_bytes)
    if final_sha != candidate_descriptor["files"]["appsbl.partition.bin"]["sha256"]:
        raise PayloadPatchError("reconstructed final candidate SHA differs from descriptor")

    def byte_array(data: bytes) -> str:
        if not data:
            return "    0x00u,\n"
        lines = []
        for at in range(0, len(data), 16):
            lines.append("    " + ", ".join(f"0x{b:02x}u" for b in data[at:at+16]) + ",\n")
        return "".join(lines)

    guard = "NX30_BRIDGE_PAYLOAD_PATCH_GENERATED_H"
    lines = [
        "/* Generated from exact pinned full APPSBL members. Base is data-only and is never flashed or executed. */",
        f"#ifndef {guard}", f"#define {guard}",
        "#include <stdint.h>",
        "#ifndef BI_PAYLOAD_PATCH_STRUCT_DEFINED",
        "#define BI_PAYLOAD_PATCH_STRUCT_DEFINED 1",
        "struct bi_payload_patch { uint32_t offset, bytes, data_offset; };",
        "#endif",
        f"#define BI_PAYLOAD_BASE_DESCRIPTOR_SHA256 {_cstr(base_descriptor_sha)}",
        f"#define BI_PAYLOAD_BASE_CANDIDATE_ID {_cstr(base_id)}",
        f"#define BI_PAYLOAD_BASE_SOURCE_SNAPSHOT_SHA256 {_cstr(source_sha)}",
        f"#define BI_PAYLOAD_BASE_SHA256 {_cstr(base_sha)}",
        f"#define BI_PAYLOAD_BASE_BYTES {APPSBL_BYTES}u",
        "#define BI_PAYLOAD_BASE_IS_DATA_ONLY 1u",
        f"#define BI_PAYLOAD_PATCH_CANDIDATE_ID {_cstr(candidate_id)}",
        f"#define BI_PAYLOAD_PATCH_CANDIDATE_DESCRIPTOR_SHA256 {_cstr(candidate_descriptor_sha)}",
        f"#define BI_PAYLOAD_PATCH_FINAL_SHA256 {_cstr(final_sha)}",
        f"#define BI_PAYLOAD_PATCH_LOAD_ECC_BITS {candidate_descriptor['load_ecc_bits']}u",
        f"#define BI_PAYLOAD_PATCH_STORAGE_ECC_BITS {candidate_descriptor['storage_ecc_bits']}u",
        f"#define BI_PAYLOAD_PATCH_COUNT {len(patches)}u",
        "static const struct bi_payload_patch BI_PAYLOAD_PATCHES[] = {",
    ]
    if patches:
        lines.extend(f"    {{{offset}u, {count}u, {data_offset}u}}," for offset, count, data_offset in patches)
    else:
        lines.append("    {0u, 0u, 0u},")
    lines += [
        "};",
        "static const uint8_t BI_PAYLOAD_PATCH_DATA[] = {",
        byte_array(bytes(patch_data)).rstrip("\n"),
        "};",
        f"#define BI_PAYLOAD_PATCH_DATA_BYTES {len(patch_data)}u",
        f"#define BI_PAYLOAD_PATCH_MAX_RANGE_BYTES {max((count for _, count, _ in patches), default=0)}u",
        f"#define BI_PAYLOAD_PATCH_MERGE_GAP {merge_gap}u",
        "#endif",
        "",
    ]
    metadata = {
        "base_candidate_id": base_id,
        "base_descriptor_sha256": base_descriptor_sha,
        "base_sha256": base_sha,
        "base_bytes": len(base_bytes),
        "candidate_id": candidate_id,
        "candidate_descriptor_sha256": candidate_descriptor_sha,
        "candidate_sha256": final_sha,
        "candidate_bytes": len(candidate_bytes),
        "board": board,
        "source_snapshot_sha256": source_sha,
        "base_load_ecc_bits": base_descriptor["load_ecc_bits"],
        "base_storage_ecc_bits": base_descriptor["storage_ecc_bits"],
        "candidate_load_ecc_bits": candidate_descriptor["load_ecc_bits"],
        "candidate_storage_ecc_bits": candidate_descriptor["storage_ecc_bits"],
        "patch_count": len(patches),
        "patch_bytes": len(patch_data),
        "patch_max_range_bytes": max((count for _, count, _ in patches), default=0),
        "patch_merge_gap": merge_gap,
        "reconstructed_sha256": final_sha,
    }
    return "\n".join(lines), metadata
