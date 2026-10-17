#!/usr/bin/env python3
"""Check every persistent QPIC mutation path rejects A/C RAM-only targets."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "drivers/mtd/nand/qpic_nand.c").read_text()

def body(name):
    start = source.index(name)
    begin = source.index("{", start)
    depth = 0
    for i in range(begin, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[begin:i + 1]
    raise AssertionError(f"unterminated function: {name}")

for name in ("qpic_nand_write_page(", "qpic_nand_mark_badblock(",
             "qpic_nand_write_oob(", "qpic_nand_blk_erase(",
             "qpic_nand_erase("):
    fn = body(name)
    assert "CONFIG_H3C_HW_A_RAM_READONLY" in fn
    assert "h3c_hw_a_nand_write_rejected()" in fn
    assert "CONFIG_H3C_HW_C_RAM_READONLY" in fn
    assert "h3c_hw_c_nand_write_rejected()" in fn

assert "#include <h3c_hw_a_readonly.h>" in source
stub_start = source.index("#elif defined(CONFIG_H3C_HW_A_RAM_READONLY) || defined(CONFIG_H3C_HW_C_RAM_READONLY)")
stubs = source[stub_start:source.index("struct bam_desc qpic_cmd_desc_fifo", stub_start)]
for api in ("rt3_fw_write_window_open", "rt3_fw_write_window_close", "rt3_fw_write_window_allows", "rt3_metadata_write_window_open", "rt3_named_nand_write_window_open", "rt3_bootconfig_write_window_open"):
    assert api in stubs, f"missing RAM-only linkable deny stub: {api}"
mtd = body("qpic_nand_mtd_params(")
assert "CONFIG_H3C_HW_A_RAM_READONLY" in source[source.index("qpic_nand_mtd_params("):]
training = source[source.index("void qpic_nand_init("):]
assert "CONFIG_H3C_HW_A_RAM_READONLY" in training
print("H3C A/C RAM-only QPIC write, mark-bad, erase, read-only attach, and training guards: PASS")
