#!/usr/bin/env python3
"""Compile the release branch extracted from common/autoboot.c and exercise it."""
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "common/autoboot.c"
DEFCFG = ROOT / "configs/ipq5018_h3c_hw_d_production_defconfig"


def main() -> None:
    source = SOURCE.read_text(encoding="utf-8")
    defconfig = DEFCFG.read_text(encoding="utf-8")
    start = source.index("#ifdef CONFIG_H3C_RELEASE_SYSTEM_DEFAULT", source.index("void autoboot_command("))
    end = source.index("#endif", start) + len("#endif")
    release_branch = source[start:end]

    # The exact D production profile does not select keyed-autoboot; therefore
    # abortboot(3) reaches U-Boot's standard any-key UART polling implementation.
    assert "CONFIG_AUTOBOOT_KEYED=y" not in defconfig
    assert "if (abortboot(3))" in release_branch
    assert release_branch.index("abortboot(3)") < release_branch.index("h3c_release_autoboot()")
    assert "getenv(" not in release_branch and "run_command_list" not in release_branch

    normal = source[source.index("static int abortboot_normal("):source.index("# endif\t/* CONFIG_AUTOBOOT_KEYED */")]
    for forbidden in ("getenv(", "run_command(", "run_command_list("):
        assert forbidden not in normal, f"standard UART abort polling executes {forbidden}"

    harness = r'''#include <assert.h>
#include <stdio.h>
static int abort_result, abort_calls, abort_delay, release_calls;
static int abortboot(int seconds) { abort_calls++; abort_delay = seconds; return abort_result; }
static void h3c_release_autoboot(void) { release_calls++; }
static void autoboot_command(const char *s) {
'''+release_branch+r'''
}
int main(void) {
 abort_result=1; autoboot_command("imported-bootcmd-must-not-run");
 assert(abort_calls==1 && abort_delay==3 && release_calls==0);
 abort_calls=abort_delay=release_calls=0; abort_result=0;
 autoboot_command("imported-altbootcmd-must-not-run");
 assert(abort_calls==1 && abort_delay==3 && release_calls==1);
 puts("release UART abort window PASS: fixed 3s, stop returns to caller, no-input dispatch unchanged");
 return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="h3c-release-uart-abort-") as tmp:
        cfile = Path(tmp) / "release_abort.c"
        exe = Path(tmp) / "release_abort"
        cfile.write_text(harness, encoding="utf-8")
        subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", "-DCONFIG_H3C_RELEASE_SYSTEM_DEFAULT",
                        str(cfile), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
