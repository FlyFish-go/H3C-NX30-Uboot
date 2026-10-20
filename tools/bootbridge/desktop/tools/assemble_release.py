#!/usr/bin/env python3
"""Fail-closed BootBridge release assembly verifier.

This tool does not sign, publish, build, or modify an executable.  It validates
an already-built unit against a canonical embedded descriptor, an immutable
catalog and referenced program/payload files, a signed OTA manifest, and
cryptographically verified prior release manifests.  The embedded descriptor
is intentionally required exactly once in the final EXE; until the build
integrates it, verification fails closed.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import subprocess
import tempfile
import zipfile
import io
import struct
import stat
import sys
from dataclasses import dataclass

REPOSITORY = "FlyFish-go/H3C-RT3000-Product-R1"
SCHEMA = "bootbridge-release-assembly-v1"
CATALOG_SCHEMA = "bootbridge-compiled-catalog-v2"
PLAN_MAGIC = b"BBDP1\x00\r\n"
MAGIC = b"BOOTBRIDGE-EMBEDDED-ASSEMBLY-V1\n"
END = b"\nBOOTBRIDGE-END-EMBEDDED-ASSEMBLY-V1\n"
MAX_EVIDENCE = 1024 * 1024
MAX_COMPONENT = 128 * 1024 * 1024
MAX_EXE = 64 * 1024 * 1024
MAX_MANIFEST = 2048
MAX_SIGNATURE = 384
NATIVE_ENGINE_VERSION = 2
BOARD_SELECTION_ENGINE_VERSION = 2
MODELS = {"NX30", "RC3000", "RT3000", "RW3000", "RX3000"}
BOARDS = {"HW-A", "HW-B", "HW-C", "HW-D"}
MANIFEST_FIELDS = ["repository", "key_id", "release_set", "sequence",
                   "architecture", "minimum_windows", "minimum_engine",
                   "unit_path", "unit_bytes", "unit_sha256", "bundle_path",
                   "bundle_bytes", "bundle_sha256"]

_PLAN_COMPILER_PATH = pathlib.Path(__file__).with_name("compile_delta_plan.py")
_PLAN_SPEC = importlib.util.spec_from_file_location("bb_release_delta_compiler", _PLAN_COMPILER_PATH)
_PLAN_COMPILER = importlib.util.module_from_spec(_PLAN_SPEC)
sys.modules[_PLAN_SPEC.name] = _PLAN_COMPILER
_PLAN_SPEC.loader.exec_module(_PLAN_COMPILER)
_PE_CATALOG_PATH = pathlib.Path(__file__).with_name("verify_pe_catalog.py")
_PE_CATALOG_SPEC = importlib.util.spec_from_file_location("bb_release_pe_catalog", _PE_CATALOG_PATH)
_PE_CATALOG = importlib.util.module_from_spec(_PE_CATALOG_SPEC)
sys.modules[_PE_CATALOG_SPEC.name] = _PE_CATALOG
_PE_CATALOG_SPEC.loader.exec_module(_PE_CATALOG)


def _pairs_no_duplicates(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ValueError("duplicate JSON key")
        out[key] = value
    return out


def canonical_json(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=True).encode("ascii")


def parse_canonical_json(raw: bytes):
    if not raw or len(raw) > MAX_EVIDENCE or any(b > 0x7e or (b < 0x20 and b not in (9, 10, 13)) for b in raw):
        raise ValueError("JSON encoding/size")
    value = json.loads(raw.decode("ascii"), object_pairs_hook=_pairs_no_duplicates)
    if canonical_json(value) != raw:
        raise ValueError("JSON must use canonical compact ASCII encoding")
    return value


def _exact_keys(value, expected, where):
    if not isinstance(value, dict) or set(value) != set(expected):
        raise ValueError(where + " fields")


def _atom(value, where):
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", value):
        raise ValueError("invalid " + where)
    return value


def _positive(value, maximum, where):
    if type(value) is not int or value <= 0 or value > maximum:
        raise ValueError("invalid " + where)
    return value


def _sha(value, where):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError("invalid " + where)
    return value


def _identity(value):
    fields = {"repository", "key_id", "release_set", "sequence",
              "architecture", "engine", "minimum_windows", "minimum_engine"}
    _exact_keys(value, fields, "identity")
    if value["repository"] != REPOSITORY:
        raise ValueError("repository")
    _atom(value["key_id"], "key id")
    _atom(value["release_set"], "release set")
    _positive(value["sequence"], 2**64 - 1, "sequence")
    if value["architecture"] not in ("x86", "x64"):
        raise ValueError("architecture")
    if value["minimum_windows"] != "6.1.7601":
        raise ValueError("minimum Windows")
    _positive(value["minimum_engine"], 2**32 - 1, "minimum engine")
    _positive(value["engine"], 2**32 - 1, "embedded engine")
    if value["engine"] != NATIVE_ENGINE_VERSION or value["minimum_engine"] > value["engine"]:
        raise ValueError("embedded engine does not match compiled engine")
    return value


def _parse_plan(raw, input_bytes):
    if len(raw) < 20 or not raw.startswith(PLAN_MAGIC):
        raise ValueError("delta program framing")
    count, data_bytes = struct.unpack_from(">IQ", raw, len(PLAN_MAGIC))
    header = len(PLAN_MAGIC) + 12
    record_bytes = count * 32
    if not 0 < count <= 262144 or header + record_bytes + data_bytes != len(raw):
        raise ValueError("delta program bounds/framing")
    ops = []
    at = header
    output_bytes = 0
    saw_literal = False
    data_start = header + record_bytes
    for _ in range(count):
        kind = raw[at]
        if raw[at + 1:at + 8] != bytes(7):
            raise ValueError("delta program reserved bytes")
        offset, size, data_offset = struct.unpack_from(">QQQ", raw, at + 8)
        if saw_literal and kind != 2:
            raise ValueError("LITERAL must be the final operation tail")
        if not size or size > 128 * 1024 * 1024 - output_bytes:
            raise ValueError("delta operation size")
        if kind == 1:  # COPY
            if offset > input_bytes or size > input_bytes - offset or data_offset:
                raise ValueError("COPY bounds")
        elif kind == 3:  # XOR
            if offset > input_bytes or size > input_bytes - offset or data_offset > data_bytes or size > data_bytes - data_offset:
                raise ValueError("XOR bounds")
        elif kind == 2:  # LITERAL
            if offset or data_offset > data_bytes or size > data_bytes - data_offset:
                raise ValueError("LITERAL bounds/order")
            saw_literal = True
        else:
            raise ValueError("unknown delta operation")
        ops.append((kind, offset, size, data_offset))
        output_bytes += size
        at += 32
    return ops, raw[data_start:], output_bytes


def _reconstruct(input_data, program):
    ops, data, output_bytes = _parse_plan(program, len(input_data))
    result = bytearray()
    for kind, offset, size, data_offset in ops:
        if kind == 1:
            result.extend(input_data[offset:offset + size])
        elif kind == 3:
            source = input_data[offset:offset + size]
            mask = data[data_offset:data_offset + size]
            result.extend(a ^ b for a, b in zip(source, mask))
        else:
            result.extend(data[data_offset:data_offset + size])
    if len(result) != output_bytes:
        raise ValueError("delta output size")
    return bytes(result), ops, data


def compile_plan_program(original, target):
    """Encode a pinned original/candidate pair as the native BBDP table format."""
    operations, data, _ = _PLAN_COMPILER.build_plan(original, target)
    if _PLAN_COMPILER.reconstruct(original, operations, data, len(target)) != target:
        raise ValueError("delta compiler reconstruction mismatch")
    kinds = {"COPY": 1, "LITERAL": 2, "XOR": 3}
    header = PLAN_MAGIC + len(operations).to_bytes(4, "big") + len(data).to_bytes(8, "big")
    table = b"".join(bytes([kinds[op.kind]]) + bytes(7) + op.offset.to_bytes(8, "big") +
                     op.size.to_bytes(8, "big") +
                     (0 if op.kind == "COPY" else op.data_offset).to_bytes(8, "big")
                     for op in operations)
    result = header + table + data
    rebuilt, _, _ = _reconstruct(original, result)
    if rebuilt != target:
        raise ValueError("native program encoding mismatch")
    return result


def _rule_set(catalog, identity, components):
    _exact_keys(catalog, {"schema", "release_set", "rules"}, "catalog")
    if catalog["schema"] != CATALOG_SCHEMA or catalog["release_set"] != identity["release_set"]:
        raise ValueError("catalog release identity")
    rules = catalog["rules"]
    if not isinstance(rules, list) or not rules:
        raise ValueError("production catalog must contain reviewed rules")
    programs = {k: v for k, v in components["programs"].items() if k != "_bytes"}
    payloads = {k: v for k, v in components["payloads"].items() if k != "_bytes"}
    used_programs, used_payloads, seen, seen_rule_ids = set(), set(), set(), set()
    inputs = {k: v for k, v in components["inputs"].items() if k != "_bytes"}
    allowed = {"id", "model", "board", "official_version", "minimum_engine_revision", "input_id", "input_bytes", "input_sha256",
               "output_bytes", "output_sha256", "program_id", "payload_id"}
    for rule in rules:
        _exact_keys(rule, allowed, "catalog rule")
        if not isinstance(rule["model"], str) or rule["model"] not in MODELS:
            raise ValueError("unknown product model")
        if not isinstance(rule["board"], str) or rule["board"] not in BOARDS:
            raise ValueError("unknown or missing hardware board")
        version = rule["official_version"]
        if not isinstance(version, str) or not 1 <= len(version.encode("utf-8")) <= 128 or any(ord(c) < 0x20 for c in version):
            raise ValueError("official version")
        rule_id = _atom(rule["id"], "rule id")
        if rule_id in seen_rule_ids:
            raise ValueError("duplicate rule id")
        seen_rule_ids.add(rule_id)
        minimum_engine = _positive(rule["minimum_engine_revision"], 2**32 - 1, "rule minimum engine")
        if minimum_engine < BOARD_SELECTION_ENGINE_VERSION:
            raise ValueError("board-bound rule requires a board-aware engine")
        if minimum_engine > identity["engine"]:
            raise ValueError("rule requires a newer engine")
        input_id = _atom(rule["input_id"], "input id")
        if input_id not in inputs:
            raise ValueError("rule references missing original BIN")
        size = _positive(rule["input_bytes"], 128 * 1024 * 1024, "rule input size")
        digest = _sha(rule["input_sha256"], "rule input hash")
        output_size = _positive(rule["output_bytes"], 128 * 1024 * 1024, "rule output size")
        output_hash = _sha(rule["output_sha256"], "rule output hash")
        program_id = _atom(rule["program_id"], "program id")
        payload_id = _atom(rule["payload_id"], "payload id")
        identity_key = (rule["model"], rule["board"], size, digest)
        if identity_key in seen:
            raise ValueError("duplicate model/input rule")
        seen.add(identity_key)
        if program_id not in programs or payload_id not in payloads:
            raise ValueError("rule references missing compiled component")
        original = inputs[input_id]
        if original["bytes"] != size or original["sha256"] != digest:
            raise ValueError("rule input does not match exact official BIN")
        payload = payloads[payload_id]
        if payload["bytes"] != output_size or payload["sha256"] != output_hash:
            raise ValueError("rule output does not match exact payload")
        rebuilt, _, _ = _reconstruct(components["inputs"]["_bytes"][input_id], components["programs"]["_bytes"][program_id])
        if len(rebuilt) != output_size or hashlib.sha256(rebuilt).hexdigest() != output_hash or rebuilt != components["payloads"]["_bytes"][payload_id]:
            raise ValueError("compiled delta does not reconstruct pinned candidate BIN")
        used_programs.add(program_id)
        used_payloads.add(payload_id)
    if used_programs != set(programs) or used_payloads != set(payloads) or {r["input_id"] for r in rules} != set(inputs):
        raise ValueError("unreferenced original, program, or payload component")
    return rules


def _cstr(value):
    return '"' + "".join("\\%03o" % b for b in value.encode("utf-8")) + '"'


def _cbytes(data):
    return "{" + (",".join("0x%02x" % b for b in data) if data else "0x00") + "}"


def render_catalog_header(evidence, component_bytes):
    """Render the C catalog and BB_PLAN arrays from the exact validated inputs."""
    identity = _identity(evidence["identity"])
    catalog = parse_canonical_json(component_bytes["catalog"])
    parsed = {}
    for name, data in component_bytes.items():
        if name.startswith("inputs/"):
            parsed.setdefault("inputs", {})[name.split("/", 1)[1]] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            parsed.setdefault("input_data", {})[name.split("/", 1)[1]] = data
        elif name.startswith("programs/"):
            parsed.setdefault("programs", {})[name.split("/", 1)[1]] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            parsed.setdefault("program_data", {})[name.split("/", 1)[1]] = data
        elif name.startswith("payloads/"):
            parsed.setdefault("payloads", {})[name.split("/", 1)[1]] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            parsed.setdefault("payload_data", {})[name.split("/", 1)[1]] = data
    parsed["inputs"]["_bytes"] = parsed["input_data"]
    parsed["programs"]["_bytes"] = parsed["program_data"]
    parsed["payloads"]["_bytes"] = parsed["payload_data"]
    rules = _rule_set(catalog, identity, parsed)
    lines = ["/* Generated from canonical release evidence; do not edit. */",
             "#ifndef BB_RELEASE_SET_ID", "#define BB_RELEASE_SET_ID " + _cstr(identity["release_set"]), "#endif",
             "#ifndef BB_ENGINE_REVISION", "#define BB_ENGINE_REVISION %dU" % identity["engine"], "#endif"]
    model_names = {"NX30": "BB_MODEL_NX30", "RC3000": "BB_MODEL_RC3000", "RT3000": "BB_MODEL_RT3000", "RW3000": "BB_MODEL_RW3000", "RX3000": "BB_MODEL_RX3000"}
    board_names = {"HW-A": "BB_BOARD_HW_A", "HW-B": "BB_BOARD_HW_B", "HW-C": "BB_BOARD_HW_C", "HW-D": "BB_BOARD_HW_D"}
    program_ids = sorted(evidence["programs"])
    program_index = {name: i for i, name in enumerate(program_ids)}
    for pid in program_ids:
        raw = component_bytes["programs/" + pid]
        ops, data, _ = _parse_plan(raw, 128 * 1024 * 1024)
        i = program_index[pid]
        lines.append("static const unsigned char bb_program_data_%d[] = %s;" % (i, _cbytes(data)))
        lines.append("static const bb_plan_op bb_program_ops_%d[] = {" % i)
        for kind, offset, size, data_offset in ops:
            ptr = "NULL" if kind == 1 else "bb_program_data_%d+%dULL" % (i, data_offset)
            lines.append("{%s,%dULL,%dULL,%s}," % ({1: "BB_PLAN_COPY", 2: "BB_PLAN_LITERAL", 3: "BB_PLAN_XOR"}[kind], offset, size, ptr))
        lines.append("};")
    for ri, rule in enumerate(rules):
        iid, pid = rule["input_id"], rule["program_id"]
        input_data = component_bytes["inputs/" + iid]
        output_data = component_bytes["payloads/" + rule["payload_id"]]
        ops, _, _ = _parse_plan(component_bytes["programs/" + pid], len(input_data))
        lines.append("static const bb_plan_recipe bb_rule_plan_%d = {%s,%dULL,%s,%dULL,%s,bb_program_ops_%d,%dU};" % (
            ri, _cstr(rule["id"]), len(input_data), _cbytes(hashlib.sha256(input_data).digest()),
            len(output_data), _cbytes(hashlib.sha256(output_data).digest()), program_index[pid], len(ops)))
    lines.append("static const bb_rule bb_generated_rules[] = {")
    for ri, rule in enumerate(rules):
        lines.append("{%s,%s,%s,%dU,{0},&bb_rule_plan_%d,%s}," % (
            model_names[rule["model"]], _cstr(rule["official_version"]), _cstr(identity["release_set"]),
            rule["minimum_engine_revision"], ri, board_names[rule["board"]]))
    lines.extend(["};", "static const bb_catalog bb_generated_catalog = {BB_RELEASE_SET_ID,bb_generated_rules,sizeof(bb_generated_rules)/sizeof(bb_generated_rules[0])};", ""])
    # This typed anchor points at the exact runtime catalog object. It carries
    # only ABI metadata; it does not duplicate rules, recipes, operations, or
    # payload bytes. The verifier whitelists the ABI metadata independently.
    lines.extend([
        "#if defined(_WIN64)",
        "#define BB_PE_CATALOG_ARCH 2U",
        "#else",
        "#define BB_PE_CATALOG_ARCH 1U",
        "#endif",
        "#pragma pack(push,1)",
        "struct bb_pe_catalog_anchor_v2 { unsigned char magic[8]; uint32_t schema,architecture,pointer_bytes; uint32_t sizes[4]; uint32_t offsets[21]; const bb_catalog *catalog; };",
        "#pragma pack(pop)",
        "#if defined(_MSC_VER)",
        "#pragma section(\".rdata$bbcat\",read)",
        "__declspec(allocate(\".rdata$bbcat\")) const struct bb_pe_catalog_anchor_v2 bb_compiled_catalog_anchor = {",
        "#else",
        "__attribute__((used,section(\".rdata$bbcat\"),aligned(1))) const struct bb_pe_catalog_anchor_v2 bb_compiled_catalog_anchor = {",
        "#endif",
        "{'B','B','P','E','C','A','T','2'},2U,BB_PE_CATALOG_ARCH,(uint32_t)sizeof(void*),",
        "{(uint32_t)sizeof(bb_catalog),(uint32_t)sizeof(bb_rule),(uint32_t)sizeof(bb_plan_recipe),(uint32_t)sizeof(bb_plan_op)},",
        "{(uint32_t)offsetof(bb_catalog,release_set),(uint32_t)offsetof(bb_catalog,rules),(uint32_t)offsetof(bb_catalog,rule_count),",
        "(uint32_t)offsetof(bb_rule,model),(uint32_t)offsetof(bb_rule,official_version),(uint32_t)offsetof(bb_rule,release_set),(uint32_t)offsetof(bb_rule,minimum_engine_revision),(uint32_t)offsetof(bb_rule,recipe),(uint32_t)offsetof(bb_rule,plan),(uint32_t)offsetof(bb_rule,board),",
        "(uint32_t)offsetof(bb_plan_recipe,id),(uint32_t)offsetof(bb_plan_recipe,input_size),(uint32_t)offsetof(bb_plan_recipe,input_sha256),(uint32_t)offsetof(bb_plan_recipe,output_size),(uint32_t)offsetof(bb_plan_recipe,output_sha256),(uint32_t)offsetof(bb_plan_recipe,operations),(uint32_t)offsetof(bb_plan_recipe,operation_count),",
        "(uint32_t)offsetof(bb_plan_op,kind),(uint32_t)offsetof(bb_plan_op,input_offset),(uint32_t)offsetof(bb_plan_op,size),(uint32_t)offsetof(bb_plan_op,bytes)},",
        "&bb_generated_catalog};",
        "#undef BB_PE_CATALOG_ARCH",
        "",
    ])
    return "\n".join(lines).encode("ascii")


def validate_evidence(evidence: dict, component_bytes: dict[str, bytes]):
    """Validate evidence and return the canonical embedded descriptor bytes.

    component_bytes keys are exactly ``catalog``, ``programs/<id>`` and
    ``payloads/<id>``. This pure function performs no filesystem or crypto I/O.
    """
    _exact_keys(evidence, {"schema", "identity", "catalog", "inputs", "programs", "payloads"}, "evidence")
    if evidence["schema"] != SCHEMA:
        raise ValueError("evidence schema")
    identity = _identity(evidence["identity"])
    expected_component_keys = {"catalog"}
    for section in ("inputs", "programs", "payloads"):
        if not isinstance(evidence[section], dict) or not evidence[section]:
            raise ValueError(section + " must be nonempty")
        for ident, spec in evidence[section].items():
            _atom(ident, section + " id")
            _exact_keys(spec, {"path", "bytes", "sha256"}, section + " component")
            if not isinstance(spec["path"], str) or not spec["path"]:
                raise ValueError(section + " path")
            expected_component_keys.add(section + "/" + ident)
            _positive(spec["bytes"], MAX_COMPONENT, section + " byte count")
            _sha(spec["sha256"], section + " hash")
    _exact_keys(evidence["catalog"], {"path", "bytes", "sha256"}, "catalog component")
    if not isinstance(evidence["catalog"]["path"], str) or not evidence["catalog"]["path"]:
        raise ValueError("catalog path")
    _positive(evidence["catalog"]["bytes"], MAX_COMPONENT, "catalog byte count")
    _sha(evidence["catalog"]["sha256"], "catalog hash")
    if set(component_bytes) != expected_component_keys:
        raise ValueError("component byte set mismatch")
    specs = {"catalog": evidence["catalog"]}
    specs.update({"inputs/" + k: v for k, v in evidence["inputs"].items()})
    specs.update({"programs/" + k: v for k, v in evidence["programs"].items()})
    specs.update({"payloads/" + k: v for k, v in evidence["payloads"].items()})
    summaries = {}
    for name, data in component_bytes.items():
        if not isinstance(data, bytes) or len(data) != specs[name]["bytes"] or hashlib.sha256(data).hexdigest() != specs[name]["sha256"]:
            raise ValueError("component size/hash mismatch: " + name)
        summaries[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    catalog = parse_canonical_json(component_bytes["catalog"])
    input_summaries = {k: summaries["inputs/" + k] for k in evidence["inputs"]}
    program_summaries = {k: summaries["programs/" + k] for k in evidence["programs"]}
    payload_summaries = {k: summaries["payloads/" + k] for k in evidence["payloads"]}
    _rule_set(catalog, identity, {
        "inputs": {**input_summaries, "_bytes": {k: component_bytes["inputs/" + k] for k in evidence["inputs"]}},
        "programs": {**program_summaries, "_bytes": {k: component_bytes["programs/" + k] for k in evidence["programs"]}},
        "payloads": {**payload_summaries, "_bytes": {k: component_bytes["payloads/" + k] for k in evidence["payloads"]}},
    })
    catalog_header = render_catalog_header(evidence, component_bytes)
    descriptor = {
        "schema": SCHEMA,
        "identity": identity,
        "catalog_sha256": summaries["catalog"]["sha256"],
        "generated_catalog_sha256": hashlib.sha256(catalog_header).hexdigest(),
        "components": summaries,
    }
    return MAGIC + canonical_json(descriptor) + END


def _parse_descriptor(exe: bytes):
    if not isinstance(exe, bytes) or not 0 < len(exe) <= MAX_EXE:
        raise ValueError("EXE size")
    if exe.count(MAGIC) != 1 or exe.count(END) != 1:
        raise ValueError("embedded descriptor markers must occur exactly once")
    start = exe.index(MAGIC)
    body_start = start + len(MAGIC)
    end = exe.find(END, body_start)
    if end < 0 or exe.find(MAGIC, end + len(END)) >= 0:
        raise ValueError("embedded descriptor framing")
    body = exe[body_start:end]
    descriptor = parse_descriptor_bytes(MAGIC + body + END)
    return descriptor, exe[start:end + len(END)]


def parse_descriptor_bytes(raw: bytes):
    if not isinstance(raw, bytes) or not raw.startswith(MAGIC) or not raw.endswith(END):
        raise ValueError("descriptor framing")
    descriptor = parse_canonical_json(raw[len(MAGIC):-len(END)])
    _exact_keys(descriptor, {"schema", "identity", "catalog_sha256", "generated_catalog_sha256", "components"}, "descriptor")
    if descriptor["schema"] != SCHEMA:
        raise ValueError("descriptor schema")
    _identity(descriptor["identity"])
    _sha(descriptor["catalog_sha256"], "descriptor catalog SHA")
    _sha(descriptor["generated_catalog_sha256"], "descriptor generated catalog SHA")
    if not isinstance(descriptor["components"], dict) or "catalog" not in descriptor["components"]:
        raise ValueError("descriptor components")
    for name, summary in descriptor["components"].items():
        _exact_keys(summary, {"bytes", "sha256"}, "component summary")
        _positive(summary["bytes"], MAX_COMPONENT, "component bytes")
        _sha(summary["sha256"], "component SHA")
        if name != "catalog" and not re.fullmatch(r"(?:inputs|programs|payloads)/[A-Za-z0-9_-]{1,64}", name):
            raise ValueError("descriptor component name")
    if descriptor["components"]["catalog"]["sha256"] != descriptor["catalog_sha256"]:
        raise ValueError("descriptor catalog hash mismatch")
    return descriptor


def descriptor_c_source(raw: bytes):
    """Generate a retained descriptor with a link-time reference to its anchor."""
    descriptor = parse_descriptor_bytes(raw)
    values = ",".join("0x%02x" % b for b in raw)
    return ("/* Generated by assemble_release.py; do not edit. */\n"
            "#include <stddef.h>\n"
            "#if defined(BO_ASSEMBLY_DESCRIPTOR_ENABLED) && BO_ASSEMBLY_DESCRIPTOR_ENABLED\n"
            "#if defined(_WIN64)\n#if !BO_ASSEMBLY_ARCH_X64\n#error descriptor_architecture_mismatch\n#endif\n"
            "#else\n#if BO_ASSEMBLY_ARCH_X64\n#error descriptor_architecture_mismatch\n#endif\n#endif\n"
            "extern const unsigned char bb_compiled_catalog_anchor[];\n"
            "#pragma pack(push,1)\nstruct bb_retained_release_descriptor { unsigned char marker[" + str(len(raw)) + "]; const void *catalog_anchor; };\n#pragma pack(pop)\n"
            "#if defined(_MSC_VER)\n#pragma section(\".rdata$bbassm\",read)\n"
            "__declspec(allocate(\".rdata$bbassm\")) const struct bb_retained_release_descriptor bb_release_descriptor = {{" + values + "},bb_compiled_catalog_anchor};\n"
            "#else\n__attribute__((used,section(\".rdata$bbassm\"),aligned(1))) const struct bb_retained_release_descriptor bb_release_descriptor = {{" + values + "},bb_compiled_catalog_anchor};\n#endif\n"
            "#else\n#if defined(_MSC_VER)\n#pragma section(\".rdata$bbassm\",read)\n"
            "__declspec(allocate(\".rdata$bbassm\")) const unsigned char bb_release_descriptor[] = {0};\n"
            "#else\n__attribute__((used,section(\".rdata$bbassm\"),aligned(1))) const unsigned char bb_release_descriptor[] = {0};\n#endif\n#endif\n")


def _parse_manifest(raw):
    if not isinstance(raw, bytes) or not 0 < len(raw) <= MAX_MANIFEST or any(b > 126 or (b < 32 and b != 10) for b in raw):
        raise ValueError("manifest encoding/size")
    lines = raw.decode("ascii").split("\n")
    if len(lines) != len(MANIFEST_FIELDS) + 2 or lines[0] != "BOOTBRIDGE-RELEASE-SET-V1" or lines[-1] != "":
        raise ValueError("manifest framing")
    out = {}
    for key, line in zip(MANIFEST_FIELDS, lines[1:-1]):
        if not line.startswith(key + "="):
            raise ValueError("manifest field order/duplicate/unknown")
        out[key] = line[len(key) + 1:]
    if out["repository"] != REPOSITORY:
        raise ValueError("manifest repository")
    _atom(out["key_id"], "manifest key id")
    _atom(out["release_set"], "manifest release set")
    if not re.fullmatch(r"[1-9][0-9]{0,19}", out["sequence"]) or int(out["sequence"]) > 2**64 - 1:
        raise ValueError("manifest sequence")
    if out["architecture"] not in ("x86", "x64") or out["minimum_windows"] != "6.1.7601":
        raise ValueError("manifest platform")
    if not re.fullmatch(r"[1-9][0-9]{0,19}", out["minimum_engine"]) or int(out["minimum_engine"]) > 2**32 - 1:
        raise ValueError("manifest engine")
    for f in ("unit_bytes", "bundle_bytes"):
        if not re.fullmatch(r"[1-9][0-9]{0,19}", out[f]):
            raise ValueError("manifest size")
    if int(out["unit_bytes"]) > MAX_EXE or int(out["bundle_bytes"]) > 128 * 1024 * 1024:
        raise ValueError("manifest size limit")
    if out["unit_path"] != "BootBridge-" + out["architecture"] + ".exe" or out["bundle_path"] != "BootBridge-" + out["architecture"] + ".zip":
        raise ValueError("manifest artifact path")
    _sha(out["unit_sha256"], "manifest unit SHA")
    _sha(out["bundle_sha256"], "manifest bundle SHA")
    return out


def _check_pe(exe, arch):
    if len(exe) < 64 or exe[:2] != b"MZ":
        raise ValueError("PE DOS header")
    pe = int.from_bytes(exe[60:64], "little")
    if pe > len(exe) - 24 or exe[pe:pe + 4] != b"PE\0\0":
        raise ValueError("PE header")
    machine = int.from_bytes(exe[pe + 4:pe + 6], "little")
    size_optional = int.from_bytes(exe[pe + 20:pe + 22], "little")
    opt = pe + 24
    if size_optional < 70 or opt + size_optional > len(exe):
        raise ValueError("PE optional header")
    magic = int.from_bytes(exe[opt:opt + 2], "little")
    want = {"x86": (0x14c, 0x10b), "x64": (0x8664, 0x20b)}[arch]
    if (machine, magic) != want:
        raise ValueError("PE architecture")
    return pe, opt, size_optional


def _check_descriptor_readonly_section(exe, marker):
    pe = int.from_bytes(exe[60:64], "little")
    section_count = int.from_bytes(exe[pe + 6:pe + 8], "little")
    optional_size = int.from_bytes(exe[pe + 20:pe + 22], "little")
    table = pe + 24 + optional_size
    start = exe.index(marker)
    end = start + len(marker)
    if not 0 < section_count <= 96 or table > len(exe) or section_count * 40 > len(exe) - table:
        raise ValueError("PE section table bounds")
    for i in range(section_count):
        section = table + i * 40
        raw_size = int.from_bytes(exe[section + 16:section + 20], "little")
        raw_offset = int.from_bytes(exe[section + 20:section + 24], "little")
        characteristics = int.from_bytes(exe[section + 36:section + 40], "little")
        if (start >= raw_offset and end <= raw_offset + raw_size and
            characteristics & 0x40000000 and not characteristics & 0x80000000):
            return
    raise ValueError("embedded descriptor is not in a read-only PE section")


def _check_bundle(bundle, exe, manifest):
    if len(bundle) != int(manifest["bundle_bytes"]) or hashlib.sha256(bundle).hexdigest() != manifest["bundle_sha256"]:
        raise ValueError("bundle size/hash")
    try:
        with zipfile.ZipFile(io.BytesIO(bundle)) as z:
            infos = z.infolist()
            if len(infos) != 1:
                raise ValueError("bundle must contain one EXE")
            i = infos[0]
            if (i.filename != manifest["unit_path"] or i.orig_filename != i.filename or
                i.is_dir() or i.flag_bits & 1 or i.compress_type != zipfile.ZIP_STORED or
                i.file_size != len(exe) or i.compress_size != i.file_size or
                (i.external_attr >> 16 & 0o170000) not in (0, 0o100000)):
                raise ValueError("bundle member")
            member = z.read(i)
    except zipfile.BadZipFile as e:
        raise ValueError("bundle ZIP") from e
    if member != exe:
        raise ValueError("bundle unit differs from signed EXE")


def _check_manifest_identity(m, identity, exe, bundle):
    expected = {
        "repository": identity["repository"], "key_id": identity["key_id"],
        "release_set": identity["release_set"], "sequence": str(identity["sequence"]),
        "architecture": identity["architecture"], "minimum_windows": identity["minimum_windows"],
        "minimum_engine": str(identity["minimum_engine"]),
        "unit_path": "BootBridge-" + identity["architecture"] + ".exe",
        "unit_bytes": str(len(exe)), "unit_sha256": hashlib.sha256(exe).hexdigest(),
        "bundle_path": "BootBridge-" + identity["architecture"] + ".zip",
        "bundle_bytes": str(len(bundle)), "bundle_sha256": hashlib.sha256(bundle).hexdigest(),
    }
    if m != expected:
        raise ValueError("signed manifest identity/artifact binding mismatch")


def _expected_pe_catalog(evidence, component_bytes):
    """Build architecture-neutral semantics for comparison with the linked PE."""
    catalog = parse_canonical_json(component_bytes["catalog"])
    identity = evidence["identity"]
    expected_rules = []
    for rule in catalog["rules"]:
        input_data = component_bytes["inputs/" + rule["input_id"]]
        program = component_bytes["programs/" + rule["program_id"]]
        ops, data, _ = _parse_plan(program, len(input_data))
        expected_rules.append({
            "id": rule["id"],
            "model": rule["model"],
            "board": rule["board"],
            "official_version": rule["official_version"],
            "minimum_engine_revision": rule["minimum_engine_revision"],
            "input_bytes": rule["input_bytes"],
            "input_sha256": bytes.fromhex(rule["input_sha256"]),
            "output_bytes": rule["output_bytes"],
            "output_sha256": bytes.fromhex(rule["output_sha256"]),
            "operations": [
                {"kind": {1: "COPY", 2: "LITERAL", 3: "XOR"}[kind],
                 "input_offset": offset, "size": size,
                 "bytes": b"" if kind == 1 else data[data_offset:data_offset + size]}
                for kind, offset, size, data_offset in ops
            ],
        })
    return {"release_set": identity["release_set"], "rules": expected_rules}


@dataclass(frozen=True)
class SignedManifest:
    raw: bytes
    signature: bytes


def verify_assembly(evidence, component_bytes, exe, bundle, manifest_raw,
                    manifest_signature, prior_history, trusted_public_keys,
                    verify_signature):
    """Pure validation core; verifier callback checks RSA signatures.

    ``prior_history`` is an ordered nonempty sequence of SignedManifest records
    representing the trusted release-history evidence available to the caller.
    This validates its signatures/order and the current step; it cannot prove
    that an external publisher supplied every historical record.
    """
    expected_descriptor = validate_evidence(evidence, component_bytes)
    descriptor, actual_descriptor = _parse_descriptor(exe)
    if actual_descriptor != expected_descriptor:
        raise ValueError("final EXE descriptor differs from reviewed assembly inputs")
    identity = evidence["identity"]
    _check_pe(exe, identity["architecture"])
    _check_descriptor_readonly_section(exe, actual_descriptor)
    pe_catalog_result = _PE_CATALOG.verify_pe_catalog(
        exe, identity["architecture"], _expected_pe_catalog(evidence, component_bytes),
        actual_descriptor)
    if int(identity["engine"]) != NATIVE_ENGINE_VERSION:
        # Keep in lockstep with the currently compiled BO_ENGINE_VERSION. Bump
        # this verifier deliberately when the native engine constant changes.
        raise ValueError("unsupported compiled engine revision")
    if not isinstance(trusted_public_keys, dict) or identity["key_id"] not in trusted_public_keys:
        raise ValueError("untrusted current key id")
    if not callable(verify_signature):
        raise ValueError("signature verifier required")
    current = _parse_manifest(manifest_raw)
    _check_manifest_identity(current, identity, exe, bundle)
    if not verify_signature(current["key_id"], trusted_public_keys[current["key_id"]], manifest_raw, manifest_signature):
        raise ValueError("current manifest signature invalid")
    _check_bundle(bundle, exe, current)
    if not isinstance(prior_history, (list, tuple)) or not prior_history:
        raise ValueError("verified prior release evidence required")
    seen_sets, previous_sequence, previous_identity = set(), 0, None
    for record in prior_history:
        if not isinstance(record, SignedManifest):
            raise ValueError("prior history record type")
        prior = _parse_manifest(record.raw)
        kid = prior["key_id"]
        if kid not in trusted_public_keys or not verify_signature(kid, trusted_public_keys[kid], record.raw, record.signature):
            raise ValueError("prior manifest signature/key invalid")
        if prior["architecture"] != identity["architecture"]:
            raise ValueError("prior architecture differs")
        seq = int(prior["sequence"])
        if seq <= previous_sequence or prior["release_set"] in seen_sets:
            raise ValueError("prior history is not strictly monotonic/unique")
        previous_sequence = seq
        seen_sets.add(prior["release_set"])
        previous_identity = prior
    if int(current["sequence"]) <= previous_sequence or current["release_set"] in seen_sets:
        raise ValueError("current release rolls back or reuses a release set")
    return {
        "status": "PASS_ASSEMBLY_CONSISTENCY_ONLY",
        "identity": identity,
        "prior_high_water": previous_sequence,
        "history_records_verified": len(prior_history),
        "exe_sha256": hashlib.sha256(exe).hexdigest(),
        "catalog_sha256": hashlib.sha256(component_bytes["catalog"]).hexdigest(),
        "program_count": len(evidence["programs"]),
        "payload_count": len(evidence["payloads"]),
        "generated_catalog_header_sha256": descriptor["generated_catalog_sha256"],
        "compiled_catalog_semantics": pe_catalog_result["status"],
        "compiled_catalog_abi_profile": pe_catalog_result["abi_profile"],
        "compiled_catalog_machine_code_use": pe_catalog_result["compiled_machine_code_use"],
        "artifact_scope": "exact embedded descriptor and signed whole-unit/bundle hashes verified; anchored read-only PE catalog data graph matches canonical evidence",
        "activation_or_hardware_qualification": "NOT_ESTABLISHED",
        "history_completeness": "NOT_ESTABLISHED_BY_THIS_TOOL",
    }


def _read_regular(path, maximum):
    p = pathlib.Path(path)
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0)
    fd = os.open(p, flags)
    try:
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_size > maximum:
            raise ValueError("not a bounded regular file: " + str(p))
        chunks, total = [], 0
        while total <= maximum:
            block = os.read(fd, min(1024 * 1024, maximum + 1 - total))
            if not block:
                break
            chunks.append(block)
            total += len(block)
        if total != st.st_size or total > maximum:
            raise ValueError("file changed/size limit: " + str(p))
        return b"".join(chunks)
    finally:
        os.close(fd)


def _openssl_verifier(key_paths):
    def verify(key_id, public_pem, raw, signature):
        if not isinstance(public_pem, bytes):
            return False
        try:
            with tempfile.TemporaryDirectory(prefix="bb-assembly-verify-") as td:
                d = pathlib.Path(td)
                pub = d / "trusted-public.pem"
                msg, sig = d / "manifest", d / "signature"
                pub.write_bytes(public_pem)
                msg.write_bytes(raw)
                sig.write_bytes(signature)
                details = subprocess.run(["openssl", "pkey", "-pubin", "-in", str(pub), "-text", "-noout"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.decode("ascii")
                if "3072 bit" not in details or "Exponent: 65537 " not in details:
                    return False
                p = subprocess.run(["openssl", "dgst", "-sha256", "-verify", str(pub), "-signature", str(sig), str(msg)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                return p.returncode == 0
        except (OSError, UnicodeError, subprocess.CalledProcessError):
            return False
    return verify


def _load_evidence(path):
    root = pathlib.Path(path).resolve().parent
    raw = _read_regular(path, MAX_EVIDENCE)
    evidence = parse_canonical_json(raw)
    paths = {"catalog": evidence["catalog"]["path"]}
    for section in ("inputs", "programs", "payloads"):
        for ident, spec in evidence[section].items():
            paths[section + "/" + ident] = spec["path"]
    data = {}
    for name, rel in paths.items():
        p = pathlib.Path(rel)
        if p.is_absolute() or ".." in p.parts:
            raise ValueError("component path must be relative and non-traversing")
        current = root
        for part in p.parts:
            current = current / part
            if current.is_symlink():
                raise ValueError("component path contains a symlink")
        data[name] = _read_regular(current, MAX_COMPONENT)
    return evidence, data


def _parse_pair(value, where):
    if "=" not in value:
        raise ValueError(where + " expects ID=PATH")
    key, path = value.split("=", 1)
    return _atom(key, where + " id"), path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    d = sub.add_parser("descriptor", help="validate evidence and emit bytes for compile-time embedding")
    d.add_argument("--evidence", required=True)
    d.add_argument("--output", required=True)
    c = sub.add_parser("compile-plan", help="compile one original/candidate BIN pair into native BBDP bytes")
    c.add_argument("--input-bin", required=True)
    c.add_argument("--payload-bin", required=True)
    c.add_argument("--output", required=True)
    v = sub.add_parser("verify", help="verify final EXE, embedded assembly, signed manifest and signed prior history")
    for arg in ("evidence", "exe", "bundle", "manifest", "signature"):
        v.add_argument("--" + arg, required=True)
    v.add_argument("--prior", action="append", default=[], metavar="MANIFEST=SIGNATURE")
    v.add_argument("--trusted-key", action="append", default=[], metavar="KEY_ID=PUBLIC_PEM")
    a = p.parse_args()
    if a.command == "descriptor":
        evidence, components = _load_evidence(a.evidence)
        descriptor = validate_evidence(evidence, components)
        with pathlib.Path(a.output).open("xb") as f:
            f.write(descriptor)
        print(json.dumps({"status": "PASS_DESCRIPTOR_INPUTS", "descriptor_bytes": len(descriptor),
                          "identity": evidence["identity"],
                          "catalog_sha256": hashlib.sha256(components["catalog"]).hexdigest()}, sort_keys=True))
        return
    if a.command == "compile-plan":
        original = _read_regular(a.input_bin, MAX_COMPONENT)
        payload = _read_regular(a.payload_bin, MAX_COMPONENT)
        program = compile_plan_program(original, payload)
        with pathlib.Path(a.output).open("xb") as f:
            f.write(program)
        print(json.dumps({"status": "PASS_SYNTHETIC_PLAN_RECONSTRUCTION_ONLY",
                          "qualification": "NOT_ESTABLISHED",
                          "program_bytes": len(program),
                          "program_sha256": hashlib.sha256(program).hexdigest(),
                          "input_sha256": hashlib.sha256(original).hexdigest(),
                          "payload_sha256": hashlib.sha256(payload).hexdigest()}, sort_keys=True))
        return
    evidence, components = _load_evidence(a.evidence)
    exe = _read_regular(a.exe, MAX_EXE)
    bundle = _read_regular(a.bundle, 128 * 1024 * 1024)
    manifest = _read_regular(a.manifest, MAX_MANIFEST)
    signature = _read_regular(a.signature, MAX_SIGNATURE)
    keys = {}
    for pair in a.trusted_key:
        kid, path = _parse_pair(pair, "trusted key")
        if kid in keys:
            raise ValueError("duplicate trusted key id")
        keys[kid] = _read_regular(path, 64 * 1024)
    history = []
    for pair in a.prior:
        if "=" not in pair:
            raise ValueError("prior expects MANIFEST=SIGNATURE")
        mp, sp = pair.split("=", 1)
        history.append(SignedManifest(_read_regular(mp, MAX_MANIFEST), _read_regular(sp, MAX_SIGNATURE)))
    result = verify_assembly(evidence, components, exe, bundle, manifest, signature,
                             history, keys, _openssl_verifier(keys))
    print(json.dumps(result, sort_keys=True))


if __name__ == "__main__":
    main()
