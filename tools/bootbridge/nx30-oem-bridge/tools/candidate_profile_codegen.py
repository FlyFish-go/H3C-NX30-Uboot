#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Emit one source-bound BFI profile and matching ENV policy for a fresh build.

The caller must supply data already bound to pinned archive members. This
module performs no device detection and never derives BSS from a stripped MBN.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path
from typing import Any


class ProfileCodegenError(ValueError):
    pass


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _hex(s: Any, label: str) -> str:
    if not isinstance(s, str) or not re.fullmatch(r"[0-9a-f]{64}", s):
        raise ProfileCodegenError(f"{label} must be a lowercase SHA-256")
    return s


def _cstr(value: str) -> str:
    return '"' + value.replace('\\', '\\\\').replace('"', '\\"') + '"'


def _oob_mask(values: list[int]) -> int:
    bit_for = {64: 1, 128: 2, 256: 4}
    try:
        return sum(bit_for[value] for value in values)
    except (KeyError, TypeError) as exc:
        raise ProfileCodegenError("unsupported Linux MTD OOB value") from exc


def elf_bss_symbols(elf: bytes) -> tuple[int, int]:
    """Read __bss_start/__bss_end from an exact ELF32 little-endian symbol table."""
    if len(elf) < 52 or elf[:7] != b"\x7fELF\x01\x01\x01":
        raise ProfileCodegenError("appsbl.elf is not ELF32 little-endian")
    shoff = struct.unpack_from("<I", elf, 32)[0]
    shentsize, shnum = struct.unpack_from("<HH", elf, 46)
    if shentsize != 40 or shnum == 0 or shoff > len(elf) or shnum > (len(elf)-shoff)//40:
        raise ProfileCodegenError("ELF section table is absent or out of bounds")
    sections = [struct.unpack_from("<10I", elf, shoff+i*40) for i in range(shnum)]
    symbols: dict[str, list[int]] = {"__bss_start": [], "__bss_end": []}
    for section in sections:
        _name, kind, _flags, _addr, off, size, link, _info, _align, entsize = section
        if kind != 2:  # SHT_SYMTAB; require the full symbol table, not stripped dynsym.
            continue
        if entsize != 16 or link >= len(sections) or off > len(elf) or size > len(elf)-off:
            raise ProfileCodegenError("ELF symbol table or linked string table is malformed")
        strings_sec = sections[link]
        stroff, strsize = strings_sec[4], strings_sec[5]
        if stroff > len(elf) or strsize > len(elf)-stroff:
            raise ProfileCodegenError("ELF symbol string table is out of bounds")
        strings = elf[stroff:stroff+strsize]
        if size % entsize:
            raise ProfileCodegenError("ELF symbol table has a partial entry")
        for at in range(off, off+size, entsize):
            name_off, value, _sz, _info, _other, _ndx = struct.unpack_from("<IIIBBH", elf, at)
            if name_off >= len(strings):
                raise ProfileCodegenError("ELF symbol name offset is invalid")
            end = strings.find(b"\0", name_off)
            if end < 0:
                raise ProfileCodegenError("ELF symbol name is unterminated")
            name = strings[name_off:end].decode("ascii", "strict")
            if name in symbols:
                symbols[name].append(value)
    if any(len(values) != 1 for values in symbols.values()):
        raise ProfileCodegenError("ELF must define exactly one __bss_start and __bss_end")
    start, end = symbols["__bss_start"][0], symbols["__bss_end"][0]
    if not start or end <= start:
        raise ProfileCodegenError("ELF BSS symbol bounds are invalid")
    return start, end


def render_headers(candidate: dict[str, Any], elf_bytes: bytes,
                   nand_tuples: dict[str, Any] | list[dict[str, Any]], build_profile: dict[str, Any],
                   bss_metadata: dict[str, Any]) -> tuple[str, str]:
    """Return (BFI profile header, BII ENV candidate policy header).

    `nand_tuples` are exact rows from `nand_schema.runtime_profiles`; the
    source build supplies only rows admitted by its pinned Linux MTD OOB facts.
    """
    required_candidate = {"board", "candidate_id", "nand_profile_id", "load_ecc_bits",
                          "storage_ecc_bits", "files", "container"}
    if not isinstance(candidate, dict) or not required_candidate.issubset(candidate):
        raise ProfileCodegenError("candidate descriptor lacks source-bound v2 fields")
    board = candidate["board"]
    if board not in "ABCD" or len(board) != 1:
        raise ProfileCodegenError("candidate board class is invalid")
    if candidate["load_ecc_bits"] not in (4, 8) or candidate["storage_ecc_bits"] not in (4, 8):
        raise ProfileCodegenError("candidate ECC modes are invalid")
    if candidate["load_ecc_bits"] != candidate["storage_ecc_bits"]:
        raise ProfileCodegenError("current QPIC/provider contract does not convert load and storage ECC")
    files = candidate["files"]
    for name in ("appsbl.mbn", "appsbl.partition.bin", "appsbl.elf"):
        if not isinstance(files.get(name), dict):
            raise ProfileCodegenError("candidate must bind MBN, padded partition, and unstripped ELF")
    part = files["appsbl.partition.bin"]
    mbn = files["appsbl.mbn"]
    elf_desc = files["appsbl.elf"]
    part_bytes = part.get("bytes")
    mbn_bytes = mbn.get("bytes")
    if type(part_bytes) is not int or part_bytes != 0x140000 or type(mbn_bytes) is not int:
        raise ProfileCodegenError("candidate partition/MBN sizes are invalid")
    part_sha, mbn_sha = _hex(part.get("sha256"), "partition SHA"), _hex(mbn.get("sha256"), "MBN SHA")
    _hex(elf_desc.get("sha256"), "ELF SHA")
    if len(elf_bytes) != elf_desc.get("bytes") or _sha(elf_bytes) != elf_desc["sha256"]:
        raise ProfileCodegenError("exact appsbl.elf bytes differ from their archive descriptor")
    if len(elf_bytes) < 52 or elf_bytes[:7] != b"\x7fELF\x01\x01\x01":
        raise ProfileCodegenError("appsbl.elf lacks the exact ELF32 little-endian header")
    elf_type, machine, version, elf_entry = struct.unpack_from("<HHII", elf_bytes, 16)
    elf_flags = struct.unpack_from("<I", elf_bytes, 36)[0]
    allowed_header_pairs = {(2, 0x05000200), (3, 0x05000202)}
    if ((elf_type, elf_flags) not in allowed_header_pairs or machine != 40 or version != 1 or
            elf_entry != 0x4A920000):
        raise ProfileCodegenError("exact ELF type/flags are outside the paired release allowlist")
    container = candidate["container"]
    expected_elf_type = "ET_EXEC" if elf_type == 2 else "ET_DYN"
    if (not isinstance(container, dict) or container.get("elf_type") != expected_elf_type or
            container.get("elf_flags") != elf_flags):
        raise ProfileCodegenError("pinned MBN parser type/flags differ from exact source ELF")
    load = container.get("load") if isinstance(container, dict) else None
    if not isinstance(load, dict):
        raise ProfileCodegenError("candidate container lacks parsed LOAD facts")
    load_offset, load_bytes = load.get("file_offset"), load.get("bytes")
    entry = container.get("entry")
    load_sha = _hex(load.get("sha256"), "LOAD SHA")
    if (type(load_offset) is not int or type(load_bytes) is not int or
            type(entry) is not int or load_offset != 0x3000 or load_bytes <= 0 or
            load_offset + load_bytes > part_bytes or entry != 0x4A920000):
        raise ProfileCodegenError("candidate LOAD/entry facts are outside the normalized range")

    if not isinstance(bss_metadata, dict):
        raise ProfileCodegenError("pinned BSS metadata is required")
    start, end = elf_bss_symbols(elf_bytes)
    expected_meta = {"board": board, "nand_profile_id": candidate["nand_profile_id"],
                     "load_ecc_bits": candidate["load_ecc_bits"],
                     "storage_ecc_bits": candidate["storage_ecc_bits"],
                     "elf_sha256": elf_desc["sha256"], "bss_start": start,
                     "bss_end": end, "bss_bytes": end-start}
    for key, value in expected_meta.items():
        if bss_metadata.get(key) != value:
            raise ProfileCodegenError(f"BSS metadata mismatch: {key}")
    limit = bss_metadata.get("sdram_limit")
    if type(limit) is not int or not entry <= start < end <= limit:
        raise ProfileCodegenError("ELF BSS exceeds the declared SDRAM bound")

    if isinstance(nand_tuples, dict):
        nand_tuples = [nand_tuples]
    if not isinstance(nand_tuples, list) or not nand_tuples or any(not isinstance(row, dict) for row in nand_tuples):
        raise ProfileCodegenError("nonempty exact NAND runtime tuple list is required")
    expected_oobs = build_profile.get("mtd_oob_bytes")
    if type(expected_oobs) is int:
        expected_oobs = [expected_oobs]
    if (not isinstance(expected_oobs, list) or not expected_oobs or
            any(type(value) is not int for value in expected_oobs) or
            len(set(expected_oobs)) != len(expected_oobs) or
            not set(expected_oobs).issubset({64, 128, 256})):
        raise ProfileCodegenError("build profile must pin a unique reviewed mtd_oob_bytes list")
    if len(nand_tuples) != len(expected_oobs):
        raise ProfileCodegenError("runtime tuple list does not cover the exact source-approved OOB set")
    first_tuple = nand_tuples[0]
    fixed_tuple_fields = ("profile_id", "model_aliases", "jedec_bytes_observed", "manufacturer_id",
                          "chip_id", "capacity_mib", "page_bytes", "erase_bytes", "raw_spare_bytes",
                          "ecc_bits", "catalogue_eligible")
    tuple_oobs = []
    for row in nand_tuples:
        if (row.get("profile_id") != candidate["nand_profile_id"] or
                row.get("ecc_bits") != candidate["storage_ecc_bits"] or
                row.get("catalogue_eligible", 0) & (1 << "ABCD".index(board)) == 0):
            raise ProfileCodegenError("NAND tuple is not eligible for this candidate board/profile")
        if any(row.get(key) != first_tuple.get(key) for key in fixed_tuple_fields):
            raise ProfileCodegenError("NAND tuple list mixes profile, ECC, identity, or physical geometry")
        oob = row.get("mtd_oob_bytes")
        if type(oob) is not int or oob not in {64, 128, 256} or oob > row.get("raw_spare_bytes", 0):
            raise ProfileCodegenError("NAND tuple has invalid Linux MTD OOB size")
        tuple_oobs.append(oob)
    if len(set(tuple_oobs)) != len(tuple_oobs) or set(tuple_oobs) != set(expected_oobs):
        raise ProfileCodegenError("runtime tuple OOBs differ from source-approved OOB values")
    nand_tuple = first_tuple
    model_aliases = nand_tuple.get("model_aliases")
    model = model_aliases[0] if isinstance(model_aliases, list) and model_aliases else None
    if not isinstance(model, str) or not model:
        raise ProfileCodegenError("canonical NAND tuple lacks an exact model alias")
    raw_spare = nand_tuple.get("raw_spare_bytes")
    geometry = ("capacity_mib", "page_bytes", "erase_bytes", "mtd_oob_bytes",
                "manufacturer_id", "chip_id", "jedec_bytes_observed", "ecc_bits")
    if any(type(nand_tuple.get(key)) is not int for key in geometry) or type(raw_spare) is not int:
        raise ProfileCodegenError("canonical NAND tuple has incomplete physical geometry")
    if raw_spare < nand_tuple["mtd_oob_bytes"]:
        raise ProfileCodegenError("Linux MTD OOB exceeds physical NAND spare")

    if not isinstance(build_profile, dict) or build_profile.get("board") != board:
        raise ProfileCodegenError("build metadata board does not match candidate")
    for key in ("target", "dt_compatible"):
        if not isinstance(build_profile.get(key), str) or not build_profile[key]:
            raise ProfileCodegenError(f"build metadata lacks {key}")
    defconfig_sha = _hex(build_profile.get("defconfig_sha256"), "defconfig SHA")
    dts_sha = _hex(build_profile.get("dts_sha256"), "DTB SHA")
    machid = build_profile.get("machid")
    if isinstance(machid, str) and re.fullmatch(r"(?:0x)?[0-9a-fA-F]{1,8}", machid):
        machid = int(machid, 16)
    if type(machid) is not int or not 0 < machid <= 0xffffffff:
        raise ProfileCodegenError("source build machid is invalid")
    env_alias = build_profile.get("env_alias")
    if env_alias is not None and (not isinstance(env_alias, str) or not re.fullmatch(r"[A-Za-z0-9_.-]{1,63}", env_alias)):
        raise ProfileCodegenError("ENV alias is malformed")
    filter_keys = build_profile.get("filter_env_root_keys", 0)
    allow_missing = build_profile.get("allow_missing_env_machid", 0)
    if filter_keys not in (0, 1) or allow_missing not in (0, 1):
        raise ProfileCodegenError("ENV policy flags must be explicit bits")

    board_enum = "ABCD".index(board) + 1
    name = str(candidate["candidate_id"])
    if not re.fullmatch(r"[A-Za-z0-9._:-]{1,160}", name):
        raise ProfileCodegenError("candidate ID contains unsupported characters")
    c_candidate = ("#ifdef BFI_PROFILE_RECORD_IMPLEMENTATION\n"
        "const bfi_candidate_record bfi_generated_candidate = {\n"
        f"    BFI_CANDIDATE_CONTROLLED_TRIAL, {_cstr(name)},\n"
        f"    {_cstr(part_sha)}, {_cstr(mbn_sha)}, {_cstr(load_sha)},\n"
        f"    0x{part_bytes:x}u, 0x{load_offset:x}u, {load_bytes}u, 0x{entry:x}u, 0x{end:x}u,\n"
        f"    {candidate['load_ecc_bits']}u, 0u\n}};\n"
        "const bfi_candidate_record *const bfi_generated_candidate_refs[] = { &bfi_generated_candidate };\n"
        "#define BFI_GENERATED_BOARD_CLASS ((bfi_board_class)"+str(board_enum)+")\n"
        "const bfi_candidate_profile bfi_generated_profile = {\n"
        f"    BFI_GENERATED_BOARD_CLASS, {_cstr(str(candidate['nand_profile_id']))},\n"
        "    \"source-bound compiled candidate; no hardware qualification\",\n"
        f"    {_cstr(build_profile['target'])}, {_cstr(defconfig_sha)}, {_cstr(dts_sha)},\n"
        f"    {_cstr(build_profile['dt_compatible'])}, 0x{machid:x}u, {_cstr(model)},\n"
        f"    {nand_tuple['jedec_bytes_observed']}u, {nand_tuple['manufacturer_id']}u, {nand_tuple['chip_id']}u,\n"
        f"    {nand_tuple['capacity_mib']}u, {nand_tuple['page_bytes']}u, {nand_tuple['erase_bytes']}u,\n"
        f"    {raw_spare}u, {nand_tuple['mtd_oob_bytes']}u, {nand_tuple['ecc_bits']}u,\n"
        "    NULL, 0u, NULL, bfi_generated_candidate_refs, 1u,\n"
        f"    {candidate['load_ecc_bits']}u, {_oob_mask(tuple_oobs)}u\n}};\n"
        "#else\n"
        "extern const bfi_candidate_record bfi_generated_candidate;\n"
        "extern const bfi_candidate_record *const bfi_generated_candidate_refs[];\n"
        "extern const bfi_candidate_profile bfi_generated_profile;\n"
        "#define BFI_GENERATED_BOARD_CLASS ((bfi_board_class)"+str(board_enum)+")\n"
        "#endif\n")
    bfi_header = "/* Generated from pinned candidate, profile, ELF, and source-build inputs. */\n" + c_candidate
    bii_header = ("/* Generated compile-time candidate policy; this adds no install qualification. */\n"
        "#define BII_EXTRA_CANDIDATE_POLICIES \\\n"
        f"    {{{_cstr(board)}, {_cstr(part_sha)}, {_cstr(f'{machid:08x}')}, "
        f"{_cstr(env_alias) if env_alias is not None else 'NULL'}, {filter_keys}u, {allow_missing}u}},\n")
    return bfi_header, bii_header


def write_headers(out_dir: Path, *args: Any, **kwargs: Any) -> tuple[Path, Path]:
    bfi, bii = render_headers(*args, **kwargs)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    bfi_path, bii_path = out_dir / "bfi_candidate_profile.generated.h", out_dir / "bii_candidate_policy.generated.h"
    bfi_path.write_text(bfi, encoding="utf-8")
    bii_path.write_text(bii, encoding="utf-8")
    return bfi_path, bii_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inputs", type=Path, required=True,
                        help="JSON envelope: candidate, nand_tuples, build_profile, bss_metadata, elf_path")
    parser.add_argument("--out-dir", type=Path, required=True)
    args = parser.parse_args()
    data = json.loads(args.inputs.read_text(encoding="utf-8"))
    elf = (args.inputs.parent / data["elf_path"]).read_bytes()
    write_headers(args.out_dir, data["candidate"], elf, data["nand_tuples"],
                  data["build_profile"], data["bss_metadata"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
