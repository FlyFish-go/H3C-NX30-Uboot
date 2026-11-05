#!/bin/sh
# h3c_fem_hosttest.sh - MP02.1 5 GHz FEM rule (ART + 0x26800).
set -eu
cd "$(dirname "$0")/.."
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
cc -std=c11 -Wall -Wextra -Werror -DH3C_FEM_HOST_TEST \
  test/h3c_fem_hosttest.c failsafe/modules/h3c_fem.c -o "$tmp"
"$tmp"
