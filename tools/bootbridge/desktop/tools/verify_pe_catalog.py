#!/usr/bin/env python3
"""Independent bounded parser for BootBridge's compiled read-only catalog graph.

This parses PE bytes only. It makes no claims about machine-code control flow.
"""
from __future__ import annotations

import struct

ANCHOR_MAGIC = b"BBPECAT2"
ANCHOR_SCHEMA = 2
ANCHOR_SCALARS = 36
ANCHOR_OFFSETS = 21
ANCHOR_POINTER_OFFSET = ANCHOR_SCALARS + ANCHOR_OFFSETS * 4
ANCHOR_SIZE_X86 = ANCHOR_POINTER_OFFSET + 4
ANCHOR_SIZE_X64 = ANCHOR_POINTER_OFFSET + 8
READ = 0x40000000
WRITE = 0x80000000
EXECUTE = 0x20000000
INITIALIZED = 0x00000040

# The field sequence is part of the anchor v2 format. The independent layout
# model below does not consume anchor-supplied offsets until after validation.
CATALOG_FIELDS = ("release_set", "rules", "rule_count")
RULE_FIELDS = ("model", "official_version", "release_set", "minimum_engine_revision", "recipe", "plan", "board")
RECIPE_FIELDS = ("id", "input_size", "input_sha256", "output_size", "output_sha256", "operations", "operation_count")
OP_FIELDS = ("kind", "input_offset", "size", "bytes")
OFFSET_NAMES = tuple("catalog." + x for x in CATALOG_FIELDS) + tuple("rule." + x for x in RULE_FIELDS) + tuple("recipe." + x for x in RECIPE_FIELDS) + tuple("op." + x for x in OP_FIELDS)


def _align(value, alignment):
    return (value + alignment - 1) & ~(alignment - 1)


def _struct_layout(fields, pointer_bytes, u64_align):
    """Return (size, offsets) for pack(8) Windows C layout."""
    at = 0
    max_align = 1
    offsets = {}
    for name, size, align in fields:
        align = min(align, 8)
        at = _align(at, align)
        offsets[name] = at
        at += size
        max_align = max(max_align, align)
    return _align(at, max_align), offsets


def _abi_profile(machine, x86_u64_align=8):
    if machine == "x64":
        p, a64 = 8, 8
        profile = "win64-pack8"
    elif machine == "x86":
        p, a64 = 4, x86_u64_align
        profile = "win32-pack8-u64-align%d" % a64
    else:
        raise ValueError("unsupported PE machine")
    cat_fields = [("release_set", p, p), ("rules", p, p), ("rule_count", p, p)]
    recipe_fields = [("id", p, p), ("input_size", 8, a64), ("input_sha256", 32, 1),
                     ("output_size", 8, a64), ("output_sha256", 32, 1),
                     ("operations", p, p), ("operation_count", p, p)]
    op_fields = [("kind", 4, 4), ("input_offset", 8, a64), ("size", 8, a64), ("bytes", p, p)]
    recipe_size, recipe = _struct_layout(recipe_fields, p, a64)
    rule_fields = [("model", 4, 4), ("official_version", p, p), ("release_set", p, p),
                   ("minimum_engine_revision", 4, 4), ("recipe", recipe_size, a64), ("plan", p, p),
                   ("board", 4, 4)]
    catalog_size, catalog = _struct_layout(cat_fields, p, a64)
    rule_size, rule = _struct_layout(rule_fields, p, a64)
    op_size, op = _struct_layout(op_fields, p, a64)
    sizes = (catalog_size, rule_size, recipe_size, op_size)
    offsets = tuple(catalog[n] for n in CATALOG_FIELDS) + tuple(rule[n] for n in RULE_FIELDS) + tuple(recipe[n] for n in RECIPE_FIELDS) + tuple(op[n] for n in OP_FIELDS)
    return profile, p, sizes, offsets


def _u16(data, at):
    return struct.unpack_from("<H", data, at)[0]


def _u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def _u64(data, at):
    return struct.unpack_from("<Q", data, at)[0]


class _PE:
    def __init__(self, data, expected_arch):
        self.data = data
        if len(data) < 64 or data[:2] != b"MZ":
            raise ValueError("PE DOS header")
        pe = _u32(data, 60)
        if pe > len(data) - 24 or data[pe:pe + 4] != b"PE\0\0":
            raise ValueError("PE signature")
        machine_code = _u16(data, pe + 4)
        self.machine = {0x14C: "x86", 0x8664: "x64"}.get(machine_code)
        if self.machine != expected_arch:
            raise ValueError("PE machine/assembly architecture mismatch")
        section_count = _u16(data, pe + 6)
        optional_size = _u16(data, pe + 20)
        opt = pe + 24
        if not 0 < section_count <= 96 or opt + optional_size > len(data):
            raise ValueError("PE header bounds")
        magic = _u16(data, opt)
        want_magic = 0x10B if self.machine == "x86" else 0x20B
        if magic != want_magic:
            raise ValueError("PE optional-header architecture")
        if self.machine == "x86":
            self.image_base = _u32(data, opt + 28)
            dir_at, num_at = opt + 96, opt + 92
            self.pointer_bytes, self.reloc_type = 4, 3
        else:
            self.image_base = _u64(data, opt + 24)
            dir_at, num_at = opt + 112, opt + 108
            self.pointer_bytes, self.reloc_type = 8, 10
        if optional_size < (96 if self.machine == "x86" else 112):
            raise ValueError("PE optional header too short")
        image_size = _u32(data, opt + 56)
        headers_size = _u32(data, opt + 60)
        section_table = opt + optional_size
        if section_table > len(data) or section_count * 40 > len(data) - section_table:
            raise ValueError("PE section table bounds")
        self.image_size = image_size
        self.headers_size = headers_size
        self.protected_spans = []
        self.recognized_pointer_slots = set()
        self.allowed_pointer_relocations = set()
        self.sections = []
        for i in range(section_count):
            s = section_table + i * 40
            name = data[s:s + 8].split(b"\0", 1)[0]
            vsize, rva, raw_size, raw_at = struct.unpack_from("<IIII", data, s + 8)
            chars = _u32(data, s + 36)
            if raw_at > len(data) or raw_size > len(data) - raw_at:
                raise ValueError("PE section raw bounds")
            if rva > image_size or max(vsize, raw_size) > image_size - rva:
                raise ValueError("PE section virtual bounds")
            self.sections.append((name, rva, vsize, raw_at, raw_size, chars))
        number_dirs = _u32(data, num_at) if num_at + 4 <= opt + optional_size else 0
        self.reloc_rva = self.reloc_size = 0
        if number_dirs > 5:
            at = dir_at + 5 * 8
            if at + 8 > opt + optional_size:
                raise ValueError("PE relocation directory header bounds")
            self.reloc_rva, self.reloc_size = struct.unpack_from("<II", data, at)
        self.relocations = self._read_relocations()

    def section_for_rva(self, rva, size, readonly=True):
        if type(rva) is not int or type(size) is not int or rva < 0 or size < 0:
            raise ValueError("invalid RVA span")
        found = []
        for section in self.sections:
            name, start, vsize, raw_at, raw_size, chars = section
            span = max(vsize, raw_size)
            if rva >= start and rva - start <= span and size <= span - (rva - start):
                delta = rva - start
                if delta > raw_size or size > raw_size - delta:
                    raise ValueError("PE span enters virtual-only section tail")
                if vsize and (delta > vsize or size > vsize - delta):
                    raise ValueError("PE span exceeds virtual section size")
                if readonly and (chars & (READ | INITIALIZED) != (READ | INITIALIZED) or chars & (WRITE | EXECUTE)):
                    raise ValueError("catalog object is not initialized read-only data")
                found.append((section, raw_at + delta))
        if len(found) != 1:
            raise ValueError("RVA span is unmapped or ambiguous")
        return found[0]

    def bytes_at_rva(self, rva, size, readonly=True):
        _, raw = self.section_for_rva(rva, size, readonly)
        return self.data[raw:raw + size]

    def protect(self, rva, size, label):
        self.section_for_rva(rva, size)
        self.protected_spans.append((rva, rva + size, label))

    def register_pointer_slot(self, field_rva, relocation_allowed):
        self.recognized_pointer_slots.add(field_rva)
        if relocation_allowed:
            self.allowed_pointer_relocations.add(field_rva)

    def validate_protected_relocations(self):
        width = self.pointer_bytes
        for start, info in self.relocations.items():
            reloc_width, reloc_type, _ = info
            end = start + reloc_width
            if start < 0 or end > self.image_size:
                raise ValueError("PE relocation target exceeds image bounds")
            if reloc_type != self.reloc_type or reloc_width != width:
                raise ValueError("PE relocation target width/type mismatch")
            overlaps = [(a, b, label) for a, b, label in self.protected_spans
                        if start < b and a < end]
            if overlaps and start not in self.allowed_pointer_relocations:
                raise ValueError("relocation intersects protected catalog data outside an exact pointer slot")
        for slot in self.recognized_pointer_slots:
            if slot in self.allowed_pointer_relocations:
                info = self.relocations.get(slot)
                if not info or info[0] != width or info[1] != self.reloc_type:
                    raise ValueError("recognized non-null pointer lacks its exact base relocation")
            elif slot in self.relocations:
                raise ValueError("null pointer slot has an unexpected base relocation")

    def _read_relocations(self):
        if not self.reloc_rva and not self.reloc_size:
            return {}
        if not self.reloc_rva or self.reloc_size < 8:
            raise ValueError("PE relocation directory framing")
        raw = self.bytes_at_rva(self.reloc_rva, self.reloc_size, readonly=False)
        relocs = {}
        at = 0
        while at < len(raw):
            if len(raw) - at < 8:
                raise ValueError("PE relocation block truncated")
            page, block_size = struct.unpack_from("<II", raw, at)
            if block_size < 8 or block_size & 1 or block_size > len(raw) - at:
                raise ValueError("PE relocation block bounds")
            for pos in range(at + 8, at + block_size, 2):
                item = _u16(raw, pos)
                kind, delta = item >> 12, item & 0x0FFF
                if kind == 0:
                    continue
                if kind != self.reloc_type:
                    raise ValueError("unsupported PE base relocation type")
                rva = page + delta
                if rva in relocs:
                    raise ValueError("duplicate PE base relocation")
                width = self.pointer_bytes
                if rva > self.image_size or width > self.image_size - rva:
                    raise ValueError("PE relocation target exceeds image bounds")
                relocs[rva] = (width, kind, pos)
            at += block_size
        if at != len(raw):
            raise ValueError("PE relocation directory trailing bytes")
        previous_end = -1
        for rva in sorted(relocs):
            width = relocs[rva][0]
            if rva < previous_end:
                raise ValueError("overlapping PE base relocation targets")
            previous_end = rva + width
        return relocs

    def pointer(self, field_rva):
        self.register_pointer_slot(field_rva, True)
        info = self.relocations.get(field_rva)
        if not info or info[0] != self.pointer_bytes or info[1] != self.reloc_type:
            raise ValueError("missing pointer base relocation")
        raw = self.bytes_at_rva(field_rva, self.pointer_bytes)
        value = int.from_bytes(raw, "little")
        if value < self.image_base:
            raise ValueError("pointer VA below image base")
        rva = value - self.image_base
        if rva >= self.image_size:
            raise ValueError("pointer VA outside image")
        return rva

    def cstring(self, rva):
        section, raw_at = self.section_for_rva(rva, 1)
        _, start, vsize, _, raw_size, _ = section
        delta = rva - start
        limit = min(raw_size - delta, (vsize - delta) if vsize else raw_size - delta)
        end = self.data.find(b"\0", raw_at, raw_at + limit)
        if end < 0:
            raise ValueError("unterminated catalog string")
        self.protect(rva, end - raw_at + 1, "cstring")
        return self.data[raw_at:end].decode("utf-8", "strict")


def _read_uint(pe, rva, width):
    return int.from_bytes(pe.bytes_at_rva(rva, width), "little")


def _verify_board_code(board_code, expected_board):
    board_names = {1: "HW-A", 2: "HW-B", 3: "HW-C", 4: "HW-D"}
    if board_names.get(board_code) != expected_board:
        raise ValueError("compiled catalog board mismatch")


def _field(base, off):
    return base + off


def _pointer_field(pe, base, off):
    return pe.pointer(_field(base, off))


def _anchor(pe, expected_arch):
    matches = []
    start = 0
    while True:
        at = pe.data.find(ANCHOR_MAGIC, start)
        if at < 0:
            break
        matches.append(at)
        start = at + 1
    if len(matches) != 1:
        raise ValueError("compiled catalog anchor must occur exactly once")
    raw_at = matches[0]
    section_hits = []
    for section in pe.sections:
        name, rva, vsize, file_at, raw_size, chars = section
        if raw_at >= file_at and raw_at - file_at <= raw_size and ANCHOR_SIZE_X86 <= raw_size - (raw_at - file_at):
            section_hits.append((section, rva + raw_at - file_at))
    if len(section_hits) != 1:
        raise ValueError("catalog anchor is unmapped or ambiguous")
    section, anchor_rva = section_hits[0]
    # Header size varies only with the final pointer width; require RO bytes for
    # the x86-sized prefix before trusting the architecture field.
    if section[5] & (READ | INITIALIZED) != (READ | INITIALIZED) or section[5] & (WRITE | EXECUTE):
        raise ValueError("catalog anchor is not initialized read-only data")
    prefix = pe.data[raw_at:raw_at + ANCHOR_POINTER_OFFSET]
    if len(prefix) != ANCHOR_POINTER_OFFSET or prefix[:8] != ANCHOR_MAGIC:
        raise ValueError("catalog anchor framing")
    schema, arch_code, pointer_bytes = struct.unpack_from("<III", prefix, 8)
    arch = {1: "x86", 2: "x64"}.get(arch_code)
    if schema != ANCHOR_SCHEMA or arch != expected_arch or pointer_bytes != pe.pointer_bytes:
        raise ValueError("catalog anchor architecture/schema")
    expected_arch_code = 1 if expected_arch == "x86" else 2
    if arch_code != expected_arch_code:
        raise ValueError("catalog anchor architecture tag")
    sizes = struct.unpack_from("<4I", prefix, 20)
    offsets = struct.unpack_from("<21I", prefix, 36)
    profiles = [_abi_profile(expected_arch)] if expected_arch == "x64" else [
        _abi_profile("x86", 4), _abi_profile("x86", 8)]
    matching = [p for p in profiles if pointer_bytes == p[1] and sizes == p[2] and offsets == p[3]]
    if not matching:
        raise ValueError("compiled catalog C ABI is not a whitelisted profile")
    profile = matching[0][0]
    full_size = ANCHOR_SIZE_X86 if expected_arch == "x86" else ANCHOR_SIZE_X64
    pe.bytes_at_rva(anchor_rva, full_size)
    pe.protect(anchor_rva, full_size, "anchor")
    catalog_rva = pe.pointer(anchor_rva + ANCHOR_POINTER_OFFSET)
    return anchor_rva, catalog_rva, profile, sizes, offsets, raw_at


def verify_pe_catalog(exe, expected_arch, expected, descriptor_marker):
    """Require the anchored PE data graph to match independently parsed evidence.

    expected uses a stable semantic schema, not the C/PE layout. The descriptor
    marker's trailing packed pointer must keep the anchor in the linked image.
    """
    pe = _PE(exe, expected_arch)
    anchor_rva, catalog_rva, profile, sizes, offsets, anchor_file = _anchor(pe, expected_arch)
    # Anchor and all reachable data must be in read-only file-backed sections.
    release_off, rules_off, count_off = offsets[:3]
    pe.section_for_rva(catalog_rva, sizes[0])
    pe.protect(catalog_rva, sizes[0], "catalog")
    release_rva = _pointer_field(pe, catalog_rva, release_off)
    rules_rva = _pointer_field(pe, catalog_rva, rules_off)
    rule_count = _read_uint(pe, catalog_rva + count_off, pe.pointer_bytes)
    if rule_count != len(expected["rules"]):
        raise ValueError("compiled catalog rule count mismatch")
    if pe.cstring(release_rva) != expected["release_set"]:
        raise ValueError("compiled catalog release set mismatch")
    _, rule_size, recipe_size, op_size = sizes
    rule_offs = offsets[3:10]
    recipe_offs = offsets[10:17]
    op_offs = offsets[17:21]
    pe.section_for_rva(rules_rva, rule_count * rule_size)
    pe.protect(rules_rva, rule_count * rule_size, "rules")
    model_names = {1: "RT3000", 2: "RW3000", 3: "RC3000", 4: "RX3000", 5: "NX30"}
    model_codes = {v: k for k, v in model_names.items()}
    kind_names = {1: "COPY", 2: "LITERAL", 3: "XOR"}
    for i, want in enumerate(expected["rules"]):
        rule_rva = rules_rva + i * rule_size
        pe.section_for_rva(rule_rva, rule_size)
        pe.protect(rule_rva, rule_size, "rule")
        model_code = _read_uint(pe, rule_rva + rule_offs[0], 4)
        if model_names.get(model_code) != want["model"]:
            raise ValueError("compiled catalog model mismatch")
        board_code = _read_uint(pe, rule_rva + rule_offs[6], 4)
        _verify_board_code(board_code, want["board"])
        if pe.cstring(_pointer_field(pe, rule_rva, rule_offs[1])) != want["official_version"]:
            raise ValueError("compiled catalog official version mismatch")
        if pe.cstring(_pointer_field(pe, rule_rva, rule_offs[2])) != expected["release_set"]:
            raise ValueError("compiled rule release set mismatch")
        if _read_uint(pe, rule_rva + rule_offs[3], 4) != want["minimum_engine_revision"]:
            raise ValueError("compiled rule minimum engine mismatch")
        plan_rva = _pointer_field(pe, rule_rva, rule_offs[5])
        pe.section_for_rva(plan_rva, recipe_size)
        pe.protect(plan_rva, recipe_size, "plan recipe")
        # The unused legacy recipe embedded in bb_rule is intentionally zeroed.
        recipe_base = rule_rva + rule_offs[4]
        if any(pe.bytes_at_rva(recipe_base, recipe_size)):
            raise ValueError("compiled legacy recipe field is not zero")
        recipe_id = pe.cstring(_pointer_field(pe, plan_rva, recipe_offs[0]))
        if recipe_id != want["id"]:
            raise ValueError("compiled plan id mismatch")
        input_size = _read_uint(pe, plan_rva + recipe_offs[1], 8)
        input_hash = pe.bytes_at_rva(plan_rva + recipe_offs[2], 32)
        output_size = _read_uint(pe, plan_rva + recipe_offs[3], 8)
        output_hash = pe.bytes_at_rva(plan_rva + recipe_offs[4], 32)
        if (input_size != want["input_bytes"] or input_hash != want["input_sha256"] or
                output_size != want["output_bytes"] or output_hash != want["output_sha256"]):
            raise ValueError("compiled plan fingerprint mismatch")
        operations_rva = _pointer_field(pe, plan_rva, recipe_offs[5])
        operation_count = _read_uint(pe, plan_rva + recipe_offs[6], pe.pointer_bytes)
        if operation_count != len(want["operations"]):
            raise ValueError("compiled operation count mismatch")
        pe.section_for_rva(operations_rva, operation_count * op_size)
        pe.protect(operations_rva, operation_count * op_size, "operation table")
        for j, expected_op in enumerate(want["operations"]):
            op_rva = operations_rva + j * op_size
            pe.section_for_rva(op_rva, op_size)
            pe.protect(op_rva, op_size, "operation")
            kind = _read_uint(pe, op_rva + op_offs[0], 4)
            input_offset = _read_uint(pe, op_rva + op_offs[1], 8)
            size = _read_uint(pe, op_rva + op_offs[2], 8)
            if (kind_names.get(kind) != expected_op["kind"] or input_offset != expected_op["input_offset"] or
                    size != expected_op["size"]):
                raise ValueError("compiled operation semantics mismatch")
            pointer_slot_rva = op_rva + op_offs[3]
            if kind == 1:
                # COPY's null pointer has no base relocation.
                pe.register_pointer_slot(pointer_slot_rva, False)
                raw_ptr = pe.bytes_at_rva(pointer_slot_rva, pe.pointer_bytes)
                if any(raw_ptr) or pointer_slot_rva in pe.relocations:
                    raise ValueError("compiled COPY op unexpectedly has a data pointer")
            else:
                data_rva = _pointer_field(pe, op_rva, op_offs[3])
                actual = pe.bytes_at_rva(data_rva, size)
                pe.protect(data_rva, size, "operation payload")
                if actual != expected_op["bytes"]:
                    raise ValueError("compiled operation payload mismatch")
    # The retained descriptor object carries one packed pointer after the exact
    # descriptor bytes; this edge is what prevents /OPT:REF from dropping anchor.
    marker_at = exe.find(descriptor_marker)
    if marker_at < 0 or exe.find(descriptor_marker, marker_at + 1) >= 0:
        raise ValueError("descriptor marker missing/duplicate for anchor retention")
    marker_rva_hits = []
    for section in pe.sections:
        _, rva, _, raw_at, raw_size, _ = section
        if marker_at >= raw_at and marker_at - raw_at <= raw_size and len(descriptor_marker) + pe.pointer_bytes <= raw_size - (marker_at - raw_at):
            marker_rva_hits.append(rva + marker_at - raw_at)
    if len(marker_rva_hits) != 1:
        raise ValueError("retained descriptor anchor pointer is unmapped")
    pointer_rva = marker_rva_hits[0] + len(descriptor_marker)
    pe.section_for_rva(marker_rva_hits[0], len(descriptor_marker) + pe.pointer_bytes)
    pe.protect(marker_rva_hits[0], len(descriptor_marker) + pe.pointer_bytes, "descriptor and anchor reference")
    if pointer_rva not in pe.relocations or pe.pointer(pointer_rva) != anchor_rva:
        raise ValueError("retained descriptor does not point to the compiled catalog anchor")
    pe.validate_protected_relocations()
    return {"status": "PASS_PE_COMPILED_DATA_MATCH", "architecture": expected_arch,
            "abi_profile": profile, "anchor_rva": anchor_rva,
            "catalog_rva": catalog_rva, "rule_count": rule_count,
            "compiled_machine_code_use": "NOT_PROVEN"}
