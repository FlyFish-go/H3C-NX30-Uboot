#!/usr/bin/env python3
"""Bounded source-path assertions for the AUTO RAM-only MIBIB reload."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "failsafe/modules/mibib.c").read_text()
layout = (root / "lib/rt3000_b_layout.c").read_text()
start = source.index("static int h3c_reload_mibib_ram_only")
brace = source.index("{", start)
depth = 0
end = None
for i in range(brace, len(source)):
    if source[i] == "{":
        depth += 1
    elif source[i] == "}":
        depth -= 1
        if depth == 0:
            end = i + 1
            break
assert end is not None
body = source[start:end]
for token in [
    "image_size != H3C_MIBIB_NAND_BYTES",
    "FW_TYPE_MIBIB_NAND",
    "mibib_header != image",
    "(const unsigned char *)image + 0x800",
    "rt3_layout_resolve_raw",
    "rt3_layout_same_physical_mode",
    "memcpy(saved, live",
    "memset(staged, 0",
    "mibib_ptable_init((unsigned int *)staged)",
    "mibib_ptable_init((unsigned int *)saved)",
    "rt3_layout_invalidate_cache()",
]:
    assert token in body, token
assert body.index("rt3_layout_resolve_raw") < body.index("mibib_ptable_init((unsigned int *)staged)")
assert "nand_write" not in body and "nand_erase" not in body
assert "saveenv" not in body and "flash " not in body
assert "int rt3_layout_resolve_raw(" in layout
assert "int rt3_layout_same_physical_mode(" in layout
assert "rt3_layout_same_physical_mode(&current, &candidate)" in body
assert "rt3_layout_same_physical_mode(&current, &installed)" in body
assert "h3c_mibib_preserve_live_aliases(staged, live, table_bytes)" in body
alias_start = source.index("static int h3c_mibib_preserve_live_aliases")
alias_body = source[alias_start:source.index("/* AUTO reload", alias_start)]
assert "dst->parts[i].start = src->parts[j].start" in alias_body
handler_start = source.index("static void mibib_reload_handler")
handler = source[handler_start:source.index("void mibib_register_uri_handlers", handler_start)]
for token in ["request->method != HTTP_POST", "request->form.count != 1", 'strcmp(request->form.values[0].name, "mibib")']:
    assert token in handler
print("HW-A/B AUTO MIBIB RAM-only source guard: PASS")
