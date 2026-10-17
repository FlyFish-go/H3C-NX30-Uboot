#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin="${TMPDIR:-/tmp}/rt3000_b_bootargs_hosttest.$$"
product="$bin.product.dtb"
rootless="$bin.rootless.dtb"
trap 'rm -f "$bin" "$product" "$rootless"' EXIT HUP INT TERM
dtc -q -I dts -O dtb -o "$product" test/rt3000_b_bootargs_hosttest.dts
dtc -q -I dts -O dtb -o "$rootless" test/rt3000_b_bootargs_rootless.dts
gcc -std=gnu99 -Wall -Wextra -Werror -Itest/rt3000_b_layout_stubs -Iinclude -Ilib/libfdt \
    -o "$bin" test/rt3000_b_bootargs_hosttest.c lib/rt3000_b_bootargs.c \
    -Wl,-l:libfdt.so.1
"$bin" "$product" "$rootless"
