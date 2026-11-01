#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
bin="${TMPDIR:-/tmp}/h3c_mainline_fdt_hosttest.$$"
trap 'rm -f "$bin" "$bin".*' EXIT HUP INT TERM
dtc -q -I dts -O dtb -o "$bin.ml.dtb" test/h3c_mainline_fdt_ml.dts
dtc -q -I dts -O dtb -o "$bin.qsdk.dtb" test/h3c_mainline_fdt_qsdk.dts
# U-Boot's own libfdt built for the host the way tools/ does (its own
# sources are not -Wextra clean, so they get the tools' flags).
inc="-DUSE_HOSTCC -include include/libfdt_env.h -Itest/h3c_mainline_fdt_stubs -Itools -idirafter include -Ilib/libfdt"
objs=
for f in fdt fdt_ro fdt_rw fdt_wip fdt_strerror; do
	gcc -std=gnu99 -O2 $inc -c -o "$bin.$f.o" lib/libfdt/$f.c
	objs="$objs $bin.$f.o"
done
gcc -std=gnu99 -Wall -Wextra -Werror $inc -o "$bin" \
    test/h3c_mainline_fdt_hosttest.c lib/h3c_mainline_fdt.c $objs
"$bin" "$bin.ml.dtb" "$bin.qsdk.dtb"
