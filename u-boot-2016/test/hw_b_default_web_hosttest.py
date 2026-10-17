#!/usr/bin/env python3
"""Exercise the actual CONFIG_H3C_DEFAULT_WEB autoboot override for HW-B."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
autoboot = (root / "common/autoboot.c").read_text()
start = autoboot.index("#elif defined(CONFIG_H3C_DEFAULT_WEB)",
                       autoboot.index("void autoboot_command(const char *s)"))
end = autoboot.index("\n#elif", start)
branch = autoboot[start:end].replace(
    "#elif defined(CONFIG_H3C_DEFAULT_WEB)",
    "#if defined(CONFIG_H3C_DEFAULT_WEB)", 1) + "\n#endif"

source = r"""
#include <stdio.h>
#include <string.h>
#define CONFIG_H3C_DEFAULT_WEB 1
static int stored_bootdelay;
static void run_default_web(const char *input, const char **output)
{
    const char *s = input;
""" + branch + r"""
    *output = s;
}

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
        return 1; \
    } \
} while (0)

int main(void)
{
    const char *output = NULL;

    stored_bootdelay = 3;
    run_default_web("bootipq", &output);
    CHECK(!strcmp(output, "httpd"));
    CHECK(stored_bootdelay == 0);

    stored_bootdelay = 9;
    run_default_web("run custom_boot", &output);
    CHECK(!strcmp(output, "httpd"));
    CHECK(stored_bootdelay == 0);

    puts("HW-B default web autoboot host tests: PASS");
    return 0;
}
"""

with tempfile.TemporaryDirectory(prefix="hw-b-default-web-") as tmp:
    c = Path(tmp) / "test.c"
    exe = Path(tmp) / "test"
    c.write_text(source)
    subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Wextra", str(c),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

