#!/usr/bin/env python3
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "failsafe/modules/nor_nand_autoecc.c"
FUNCTIONS = [
    "static const char *state_name(",
    "static const char *reason_name(",
    "static int json_append(",
    "static int format_json(",
    "static void print_probe_result(",
]

def extract_function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    state = "normal"
    escaped = False
    i = opening
    while i < len(source):
        ch = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if state == "string" or state == "char":
            if escaped:
                escaped = False
            elif ch == "\\":
                escaped = True
            elif (state == "string" and ch == '"') or (state == "char" and ch == "'"):
                state = "normal"
        elif state == "line":
            if ch == "\n": state = "normal"
        elif state == "comment":
            if ch == "*" and nxt == "/":
                state = "normal"
                i += 1
        else:
            if ch == '"': state = "string"
            elif ch == "'": state = "char"
            elif ch == "/" and nxt == "/": state = "line"; i += 1
            elif ch == "/" and nxt == "*": state = "comment"; i += 1
            elif ch == "{": depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return source[start:i + 1]
        i += 1
    raise AssertionError("unterminated function: " + signature)

source = SOURCE.read_text()
functions = "\n\n".join(extract_function(source, sig) for sig in FUNCTIONS)
harness = r'''#define NX30_NOR_AUTOECC_HOST_TEST
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include "failsafe/modules/nor_nand_autoecc_core.h"
'''+functions+r'''
int main(void) {
    struct nx30_nor_autoecc_result r;
    char output[4096];
    memset(&r, 0, sizeof(r));
    r.state = NX30_NOR_AUTOECC_STATE_ECC8;
    r.selected_read_ecc = 8;
    r.samples_ecc4 = 128; r.samples_ecc8 = 128;
    r.valid_ecc4 = 0; r.valid_ecc8 = 3;
    r.reason = NX30_NOR_AUTOECC_REASON_NONE;
    r.evidence_ecc8_count = 2;
    r.evidence_ecc8[0] = 0x00100000ULL;
    r.evidence_ecc8[1] = 0x00300000ULL;
    if (format_json(output, sizeof(output), &r)) return 2;
    puts(output);
    print_probe_result(&r);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="nor-autoecc-format-") as td:
    cfile = Path(td) / "format_test.c"
    exe = Path(td) / "format_test"
    cfile.write_text(harness)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT), str(cfile), "-o", str(exe)], check=True)
    proc = subprocess.run([str(exe)], check=True, text=True, capture_output=True)
    lines = proc.stdout.splitlines()
    payload = json.loads(lines[0])
    assert payload["status"] == "ecc8" and payload["selected_read_ecc"] == 8
    assert payload["evidence_ecc8"] == ["0x00100000", "0x00300000"]
    assert "\nECC8 evidence offsets:" in proc.stdout
    assert "\\nECC8 evidence offsets:" not in proc.stdout
    assert proc.stdout.endswith("\n")
print("NOR_AUTOECC_PRODUCTION_JSON_CLI_HOSTTEST_PASS")
