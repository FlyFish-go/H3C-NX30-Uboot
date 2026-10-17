#!/usr/bin/env python3
import pathlib
import re
import subprocess
import tempfile

repo = pathlib.Path(__file__).resolve().parents[2]
source = repo / "u-boot-2016" / "test" / "hw_a_buttons_hosttest.c"

with tempfile.TemporaryDirectory(prefix="hw-a-buttons-") as temp:
    binary = pathlib.Path(temp) / "hw-a-buttons-hosttest"
    subprocess.run([
        "cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
        str(source), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

dts = repo / "u-boot-2016" / "arch" / "arm" / "dts" / "ipq5018-h3c-hw-a.dts"
text = dts.read_text()
green = re.search(r"status-green\s*\{([^}]*)\}", text, re.S)
red = re.search(r"status-red\s*\{([^}]*)\}", text, re.S)
assert green and re.search(r"gpio\s*=\s*<24>", green.group(1))
assert re.search(r"active_level\s*=\s*<GPIO_ACTIVE_HIGH>", green.group(1))
assert re.search(r"pull\s*=\s*<GPIO_PULL_UP>", green.group(1))
assert red and re.search(r"gpio\s*=\s*<23>", red.group(1))
assert re.search(r"active_level\s*=\s*<GPIO_ACTIVE_HIGH>", red.group(1))
assert re.search(r"pull\s*=\s*<GPIO_PULL_UP>", red.group(1))
assert not re.search(r"gpio\s*=\s*<(17|19)>", text)
print("HW-A LED DTS mapping: green GPIO24, red GPIO23, unused GPIO17/19: PASS")
