#!/usr/bin/env python3
"""Host-test the RT3000-B production MAC parser and APPSBLENV-only scope."""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
module = root / "failsafe/modules/mac_rt3000_b.c"
src = module.read_text()
match = re.search(r"static int mac_hex\(.*?\n}\n\nstatic int parse_unicast_mac\(.*?\n}\n", src, re.S)
assert match, "production MAC parser not found"
parser = match.group(0).replace("uchar mac[6]", "unsigned char mac[6]")
assert 'failsafe_env_set_persistent("ethaddr", formatted)' in src
assert 'request->form.count != 1' in src
assert "eth1addr" not in src and "ART" not in src and "qpic" not in src.lower()
assert "raw" not in src.lower()
lines = [
    "#include <stddef.h>", "#include <errno.h>", "typedef unsigned char uchar;",
    parser,
    "#include <stdio.h>", "#include <string.h>",
    "static int check(const char *s, size_t n, int expected) {",
    "    unsigned char mac[6];",
    "    int result = parse_unicast_mac((const unsigned char *)s, n, mac);",
    "    if (!!result == !!expected) return 0;",
    "    fprintf(stderr, \"unexpected parse result for %s: %d\\n\", s, result);",
    "    return 1;", "}", "int main(void) {", "    int failed = 0;",
    '    failed |= check("02:11:22:33:44:55", 17, 0);',
    '    failed |= check("aa:bb:cc:dd:ee:f0", 17, 0);',
    '    failed |= check("01:11:22:33:44:55", 17, 1);',
    '    failed |= check("00:00:00:00:00:00", 17, 1);',
    '    failed |= check("ff:ff:ff:ff:ff:ff", 17, 1);',
    '    failed |= check("02:11:22:33:44:5g", 17, 1);',
    '    failed |= check("02-11-22-33-44-55", 17, 1);',
    '    failed |= check("02:11:22:33:44:55", 16, 1);',
    "    return failed;", "}"
]
c_test = "\n".join(lines) + "\n"
with tempfile.TemporaryDirectory() as d:
    c = pathlib.Path(d) / "test.c"
    exe = pathlib.Path(d) / "test"
    c.write_text(c_test)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: production parser accepts only strict valid unicast base MACs; setter is ethaddr/APPSBLENV-scoped")
