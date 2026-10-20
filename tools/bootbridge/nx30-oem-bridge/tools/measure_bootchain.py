#!/usr/bin/env python3
"""Parse saved, ordinary-text boot-chain observations without touching a device.

This tool deliberately reports evidence and gaps; it never grants installation
qualification.  Input files must be captures made elsewhere.
"""

from __future__ import annotations

import argparse
import json
import re
import shlex
from pathlib import Path
from typing import Any


WORD_ROW = re.compile(r"^\s*([0-9a-fA-F]{8}):\s+(.*)$")
MD_COMMAND = re.compile(r"\bmd\.l\s+(0x)?([0-9a-fA-F]+)\s+(0x)?([0-9a-fA-F]+)\b")


class EvidenceError(ValueError):
    pass


def _read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="ascii").replace("\r\n", "\n").replace("\r", "\n")
    except (OSError, UnicodeError) as exc:
        raise EvidenceError(f"cannot read ASCII evidence {path}: {exc}") from exc


def parse_memory_dump(path: Path) -> dict[str, Any]:
    """Parse one U-Boot `md.l` transcript and retain values as raw words only."""
    text = _read_text(path)
    commands = list(MD_COMMAND.finditer(text))
    if len(commands) != 1:
        raise EvidenceError(f"expected exactly one md.l command in {path}, found {len(commands)}")
    command = commands[0]
    base = int(command.group(2), 16)
    count = int(command.group(4), 16)
    if base & 3 or count <= 0 or count > 0x1000:
        raise EvidenceError("md.l address/count is unaligned or outside parser bounds")

    words: list[dict[str, int]] = []
    expected = base
    for line in text.splitlines():
        match = WORD_ROW.match(line)
        if not match:
            continue
        row_address = int(match.group(1), 16)
        values: list[int] = []
        for token in match.group(2).split():
            if not re.fullmatch(r"[0-9a-fA-F]{8}", token):
                break  # U-Boot appends an ASCII rendering after the words.
            values.append(int(token, 16))
        if not 1 <= len(values) <= 4:
            raise EvidenceError(f"malformed md.l data row at 0x{row_address:08x}")
        if row_address != expected:
            raise EvidenceError(f"memory rows are discontinuous: expected 0x{expected:08x}, got 0x{row_address:08x}")
        for value in values:
            words.append({"address": expected, "value": value})
            expected += 4
        if len(words) > count:
            raise EvidenceError("memory dump contains more words than the command requested")
    if len(words) != count:
        raise EvidenceError(f"md.l requested {count} words but transcript contains {len(words)}")
    return {
        "capture_command": command.group(0),
        "base_address": f"0x{base:08x}",
        "word_count": count,
        "covered_bytes": count * 4,
        "raw_words": [{"address": f"0x{item['address']:08x}", "value": f"0x{item['value']:08x}"} for item in words],
    }


def resolve_r012_sbl1_chain(shared_capture: dict[str, Any], flash_capture: dict[str, Any]) -> dict[str, Any]:
    """Check documented R012 pointer/field offsets, without claiming freshness."""
    shared_base = int(shared_capture["base_address"], 16)
    shared_words = {int(item["address"], 16): int(item["value"], 16) for item in shared_capture["raw_words"]}
    expected_shared = 0x08C08420
    if shared_base != expected_shared:
        return {"status": "UNKNOWN", "reason": f"shared capture base must be source-derived 0x{expected_shared:08x}", "selected_ecc": "UNKNOWN"}
    if expected_shared + 0x2C not in shared_words:
        return {"status": "UNKNOWN", "reason": "shared capture does not cover the source-defined pointer at +0x2c", "selected_ecc": "UNKNOWN"}
    pointer = shared_words[expected_shared + 0x2C]
    if pointer & 3 or int(flash_capture["base_address"], 16) != pointer:
        return {"status": "UNKNOWN", "reason": "indirect flash-info capture address does not equal shared+0x2c pointer", "pointer": f"0x{pointer:08x}", "selected_ecc": "UNKNOWN"}
    flash_words = {int(item["address"], 16): int(item["value"], 16) for item in flash_capture["raw_words"]}
    if pointer + 0x20 > max(flash_words, default=0):
        return {"status": "UNKNOWN", "reason": "flash-info capture does not cover the nested NAND parameter mode at +0x20", "pointer": f"0x{pointer:08x}", "selected_ecc": "UNKNOWN"}
    type_tag = flash_words.get(pointer + 0x0C)
    # The verified call-chain record says the helper returns the NAND-param
    # beginning at flash-info +0x10 and the initializer reads NAND-param +0x10.
    # Thus the recorded ECC mode field is effective flash-info +0x20. The
    # first word at +0x10 is not the ECC selection field.
    parameter = flash_words.get(pointer + 0x20)
    if type_tag != 2:
        return {"status": "UNKNOWN", "reason": "R012 SBL1 source type gate (+0x0c == 2) did not match", "pointer": f"0x{pointer:08x}", "type_tag": f"0x{type_tag:08x}" if type_tag is not None else "UNKNOWN", "selected_ecc": "UNKNOWN"}
    selected = {0: 4, 2: 8}.get(parameter)
    return {
        "status": "SOURCE_PATH_VALUES_MATCH" if selected else "UNKNOWN",
        "pointer": f"0x{pointer:08x}",
        "type_tag": "0x00000002",
        "nand_parameter_base": f"0x{pointer + 0x10:08x}",
        "nand_parameter_base_word": f"0x{flash_words[pointer + 0x10]:08x}" if pointer + 0x10 in flash_words else "UNKNOWN",
        "mode_field_address": f"0x{pointer + 0x20:08x}",
        "parameter": f"0x{parameter:08x}" if parameter is not None else "UNKNOWN",
        "selected_ecc": selected if selected else "UNKNOWN",
        "qualification": "NONE; source-derived values do not prove SRAM producer, lifetime, actual boot source, or APPSBL storage format",
    }


def resolve_repeated_r012_sbl1_chain(shared_a: dict[str, Any], shared_b: dict[str, Any], flash_a: dict[str, Any], flash_b: dict[str, Any]) -> dict[str, Any]:
    """Resolve two read pairs only when both fixed and indirect captures match exactly."""
    if shared_a["raw_words"] != shared_b["raw_words"]:
        return {"status": "UNKNOWN", "reason": "the two shared-block reads differ", "selected_ecc": "UNKNOWN", "stability": "MISMATCH"}
    if flash_a["raw_words"] != flash_b["raw_words"]:
        return {"status": "UNKNOWN", "reason": "the two indirect flash-info reads differ", "selected_ecc": "UNKNOWN", "stability": "MISMATCH"}
    resolved = resolve_r012_sbl1_chain(shared_a, flash_a)
    resolved["stability"] = "STABLE_DOUBLE_READ"
    resolved["stability_scope"] = "byte-for-byte equality of two saved md.l transcripts at each address; does not establish producer identity or freshness"
    return resolved


def parse_bootargs(path: Path) -> dict[str, Any]:
    text = _read_text(path)
    lines = [line.strip() for line in text.splitlines() if re.search(r"\bbootargs\s*=", line)]
    if len(lines) != 1:
        raise EvidenceError(f"expected exactly one bootargs assignment in {path}, found {len(lines)}")
    value = lines[0].split("bootargs=", 1)[1].strip()
    tokens = shlex.split(value)
    keys: dict[str, list[str]] = {"nandspare": [], "nandeccbits": [], "cnss2.skip_radio_bmap": []}
    for token in tokens:
        if "=" not in token:
            continue
        key, item = token.split("=", 1)
        if key in keys:
            keys[key].append(item)
    unique = all(len(values) <= 1 for values in keys.values())
    handoff = "UNKNOWN"
    if unique and len(keys["nandspare"]) == 1 and len(keys["nandeccbits"]) == 1:
        spare, ecc = keys["nandspare"][0], keys["nandeccbits"][0]
        if spare in {"64", "128"} and ecc in {"4", "8"}:
            handoff = f"{spare}/{ecc}"
    return {
        "nand_handoff_parameters": handoff,
        "nandspare_occurrences": keys["nandspare"],
        "nandeccbits_occurrences": keys["nandeccbits"],
        "duplicate_relevant_keys": not unique,
        "skip_radio_bmap": keys["cnss2.skip_radio_bmap"],
        "interpretation": "ENV bootargs capture only; it does not establish PBL selection or APPSBL on-flash ECC",
    }


def parse_nand_info(path: Path) -> dict[str, Any]:
    text = _read_text(path)
    device = re.search(r"Device\s+\d+:\s*([^,\r\n]+),\s*sector size\s+([0-9]+)\s*KiB", text, re.I)
    page = re.search(r"Page size\s+([0-9]+)\s*b", text, re.I)
    oob = re.search(r"OOB size\s+([0-9]+)\s*b", text, re.I)
    erase = re.search(r"Erase size\s+([0-9]+)\s*b", text, re.I)
    if not all((device, page, oob, erase)):
        raise EvidenceError(f"incomplete nand info transcript: {path}")
    return {
        "device_label": device.group(1).strip(),
        "reported_sector_bytes": int(device.group(2)) * 1024,
        "page_bytes": int(page.group(1)),
        "oob_bytes": int(oob.group(1)),
        "erase_bytes": int(erase.group(1)),
        "jedec_id": "UNKNOWN",
        "full_density_bytes": "UNKNOWN",
        "stored_page_ecc_format": "UNKNOWN",
        "interpretation": "NAND geometry report; does not measure the ECC encoding of existing pages",
    }


def _load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise EvidenceError(f"cannot read JSON evidence {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise EvidenceError(f"expected JSON object in {path}")
    return value


def parse_bridge4_history(install_path: Path, cold_path: Path, final_path: Path) -> dict[str, Any]:
    install = _load_json(install_path)
    cold = _load_json(cold_path)
    final = _load_json(final_path)
    candidate = install.get("candidate", {})
    return {
        "historical_bridge4_install": install.get("result", "UNKNOWN"),
        "historical_candidate_sha256": candidate.get("sha256", "UNKNOWN"),
        "historical_target": {
            "offset": install.get("target", {}).get("offset", "UNKNOWN"),
            "size": install.get("target", {}).get("size", "UNKNOWN"),
            "name": install.get("target", {}).get("name", "UNKNOWN"),
        },
        "historical_cold_boot": cold.get("result", "UNKNOWN"),
        "historical_final_scope": final.get("scope", "UNKNOWN"),
        "historical_final_readback_exact": final.get("full9MiB_exact_expected", False),
        "historical_sample_id": final.get("sample_id", "UNKNOWN"),
        "transfer_to_current_candidate": False,
        "interpretation": "historical evidence is scoped to its recorded sample, old candidate, and operation; it does not qualify a new candidate or infer current PBL/APPSBL measurements",
    }


def build_report(flash: dict[str, Any], bootargs: dict[str, Any], nand: dict[str, Any], history: dict[str, Any], sbl_chain: dict[str, Any] | None = None) -> dict[str, Any]:
    raw_by_address = {item["address"]: item["value"] for item in flash["raw_words"]}
    # The reviewed R012 SBL1 disassembly uses a separate shared-structure
    # pointer chain: shared+0x2c -> flash-info; flash-info+0x0c must be 2.
    # The helper/consumer chain uses NAND-param at +0x10 and reads its +0x10,
    # effective flash-info+0x20, as the PBL value (0=>ECC4, 2=>ECC8). A dump whose
    # address was chosen by the operator is not evidence that this chain points
    # there. Keep this capture raw until the pointer chain itself is captured.
    flash_value = raw_by_address.get("0x08c03020")
    return {
        "schema": 1,
        "mode": "OFFLINE_CAPTURE_PARSE_ONLY",
        "installation_qualification": "UNAVAILABLE",
        "current_capture": {
            "flash_parameter_memory": flash,
            "bootargs": bootargs,
            "nand_geometry": nand,
            "earlyloader_pointer_chain": {
                **(sbl_chain or {
                "status": "UNKNOWN",
                "reason": "capture is a direct md.l at 0x08c03000; this does not include the R012 SBL1 shared block pointer at shared+0x2c, nor prove that current SRAM contents retain the SBL handoff",
                "selected_ecc": "UNKNOWN",
                }),
                "captured_word_at_plus_0x0c": raw_by_address.get("0x08c0300c"),
                "captured_word_at_plus_0x10": raw_by_address.get("0x08c03010"),
                "captured_word_at_plus_0x20": raw_by_address.get("0x08c03020"),
                "interpreted_pbl_parameter": (sbl_chain or {}).get("parameter", "UNKNOWN"),
                "source_semantics": "verified R012 SBL1 disassembly: helper gates on flash-info +0x0c == 2, then ADD.W returns the inline address flash-info +0x10; initializer reads returned NAND-parameter +0x10 (effective flash-info +0x20), where 0 and 2 select ECC4 and ECC8 respectively; +0x10 itself is not the selector",
                "source_component_sha256": "c8bc5e1d35a3092dc1292d1b8ba13061082e08992a2cb07c05dbf7561a1244b5",
            },
            "appsbl_storage": {
                "low_copy_ecc": "UNKNOWN",
                "high_copy_ecc": "UNKNOWN",
                "per_page_coverage": "NONE_IN_CURRENT_CAPTURE",
            },
            "observed_sample_identity": "UNKNOWN_FROM_THESE_THREE_FILES",
        },
        "historical_bridge4": history,
        "missing_evidence": [
            "proof that the shared block and pointed structure remain live and are produced by the fixed R012 SBL1 on this boot, not stale or overwritten RAM",
            "current full boot-source and actual APPSBL-copy selection evidence tied to this session",
            "independent ECC4/ECC8 per-page read/validation measurements for both physical APPSBL copies, with extent coverage and correction/failure counts",
            "current JEDEC identity/full NAND density and candidate-specific compatibility; neither follows from the old bridge4 result or candidate hash",
        ],
        "qualification_rule": "Do not map catalog, machid, ENV/bootargs, raw word patterns, sample label, or historical candidate hash to a current boot-chain qualification.",
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--flash-params", type=Path, required=True)
    parser.add_argument("--bootargs", type=Path, required=True)
    parser.add_argument("--nand-info", type=Path, required=True)
    parser.add_argument("--sbl-shared", type=Path, help="optional saved md.l capture at the fixed R012 SBL1 shared-block base")
    parser.add_argument("--sbl-flash-info", type=Path, help="optional saved md.l capture at the pointer read from shared+0x2c")
    parser.add_argument("--sbl-shared-repeat", type=Path, help="second read of the fixed shared-block base")
    parser.add_argument("--sbl-flash-info-repeat", type=Path, help="second read at the captured indirect pointer")
    parser.add_argument("--bridge4-install", type=Path, required=True)
    parser.add_argument("--bridge4-cold", type=Path, required=True)
    parser.add_argument("--bridge4-final", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        supplied = [args.sbl_shared, args.sbl_flash_info, args.sbl_shared_repeat, args.sbl_flash_info_repeat]
        if any(supplied) and not all(supplied):
            raise EvidenceError("all four --sbl-shared/--sbl-flash-info repeat captures must be supplied together")
        sbl_chain = None
        if args.sbl_shared:
            sbl_chain = resolve_repeated_r012_sbl1_chain(
                parse_memory_dump(args.sbl_shared), parse_memory_dump(args.sbl_shared_repeat),
                parse_memory_dump(args.sbl_flash_info), parse_memory_dump(args.sbl_flash_info_repeat),
            )
        report = build_report(
            parse_memory_dump(args.flash_params),
            parse_bootargs(args.bootargs),
            parse_nand_info(args.nand_info),
            parse_bridge4_history(args.bridge4_install, args.bridge4_cold, args.bridge4_final),
            sbl_chain,
        )
    except EvidenceError as exc:
        parser.error(str(exc))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"status": "PARSED_WITH_QUALIFICATION_UNKNOWN", "output": str(args.output)}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
