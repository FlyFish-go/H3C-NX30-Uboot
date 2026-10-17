#!/usr/bin/env python3
"""Compile the candidate update_bootargs() against minimal U-Boot host stubs."""
import argparse
import os
import subprocess
import tempfile
from pathlib import Path


HARNESS = r"""
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CONFIG_SYS_CBSIZE 64
#define CMD_RET_SUCCESS 0
#define CMD_RET_FAILURE 1
#define FIT_BOOTARGS_PROP "bootargs"
#define abort() do { fprintf(stderr, "assertion failed at %s:%d\n", __FILE__, __LINE__); exit(1); } while (0)
#define getenv host_getenv
#define setenv host_setenv
static char old_args[128] = "old-base=1";
static char fs_args[128] = "root=/dev/test";
static int fail_setenv, setenv_calls;
static const void *fit_args;
static int fit_args_len;
static char *host_getenv(const char *key) {
    if (!strcmp(key, "bootargs")) return old_args;
    if (!strcmp(key, "fsbootargs")) return fs_args[0] ? fs_args : NULL;
    return NULL;
}
static int host_setenv(const char *key, const char *value) {
    if (strcmp(key, "bootargs")) return -1;
    setenv_calls++;
    if (fail_setenv) return -1;
    if (strlen(value) >= sizeof(old_args)) return -1;
    strcpy(old_args, value);
    return 0;
}
static const void *fdt_getprop(void *addr, int node, const char *name, int *len) {
    (void)addr; (void)node; (void)name;
    *len = fit_args_len;
    return fit_args;
}
/* Exact production function is inserted here. */
@@FUNCTION@@
int main(void) {
    char original[sizeof(old_args)];
    strcpy(original, old_args);
    setenv_calls = 0;
    if (update_bootargs(NULL, 0, 0) != CMD_RET_SUCCESS || setenv_calls != 1 ||
        strcmp(old_args, "old-base=1 root=/dev/test rootwait")) abort();

    /* Composition overflow must not erase or replace the prior environment. */
    strcpy(old_args, original);
    fit_args = NULL; fit_args_len = 0;
    memset(fs_args, 'x', sizeof(fs_args) - 1); fs_args[sizeof(fs_args) - 1] = '\0';
    setenv_calls = 0;
    if (update_bootargs(NULL, 0, 0) != CMD_RET_FAILURE || setenv_calls != 0 ||
        strcmp(old_args, original)) abort();
    strcpy(fs_args, "root=/dev/test");

    /* A failed setenv keeps the old value and is propagated to the caller. */
    strcpy(old_args, original);
    fail_setenv = 1; setenv_calls = 0;
    if (update_bootargs(NULL, 0, 0) != CMD_RET_FAILURE || setenv_calls != 1 ||
        strcmp(old_args, original)) abort();

    /* Missing root arguments fail before setenv and leave the old value intact. */
    strcpy(old_args, original); fs_args[0] = '\0';
    fail_setenv = 0; setenv_calls = 0;
    if (update_bootargs(NULL, 0, 0) != CMD_RET_FAILURE || setenv_calls != 0 ||
        strcmp(old_args, original)) abort();
    puts("BOOTARGS_HOSTTEST_PASS");
    return 0;
}
"""


def extract_function(source):
    start = source.index("static int update_bootargs(")
    end = source.index("/*\n * Set the root device", start)
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    source = args.source.resolve().read_text()
    function = extract_function(source)
    if 'setenv("bootargs", NULL)' in function or function.count('setenv("bootargs", cmd_line)') != 2:
        raise SystemExit("bootargs source invariant failed: expected two single-commit branches")
    if 'ret = setenv("bootargs", cmd_line);' not in function or \
       'if (setenv("bootargs", cmd_line))' not in function:
        raise SystemExit("bootargs source invariant failed: setenv errors must propagate")
    generated = HARNESS.replace("@@FUNCTION@@", function)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="bootargs-hosttest-") as td:
        cfile = Path(td) / "bootargs_hosttest.c"
        binary = Path(td) / "bootargs_hosttest"
        cfile.write_text(generated)
        subprocess.run(["gcc", "-std=gnu11", "-O1", "-g", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-fno-sanitize-recover=all",
                        str(cfile), "-o", str(binary)], check=True)
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = "detect_leaks=0"
        completed = subprocess.run([str(binary)], capture_output=True, text=True, env=env)
    if completed.returncode:
        raise SystemExit(completed.stderr or f"host test exited {completed.returncode}")
    report = args.output_dir / "bootargs-hosttest.txt"
    report.write_text(completed.stdout)
    print(completed.stdout.strip())
    print(f"ASAN_UBSAN=PASS output={report}")


if __name__ == "__main__":
    main()
