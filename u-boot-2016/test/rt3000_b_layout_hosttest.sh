#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin="${TMPDIR:-/tmp}/rt3000_b_layout_hosttest.$$"
trap "rm -f $bin" EXIT HUP INT TERM
gcc -std=gnu99 -Wall -Wextra -Itest/rt3000_b_layout_stubs -Iinclude -Itest -o "$bin" test/rt3000_b_layout_hosttest.c
"$bin"
