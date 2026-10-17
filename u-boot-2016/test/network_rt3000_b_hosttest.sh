#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out="$(mktemp)"
trap 'rm -f "$out"' EXIT
gcc -std=gnu99 -Wall -Wextra -Itest/network_stubs -Ifailsafe/modules \
    test/network_rt3000_b_hosttest.c -o "$out"
"$out"
