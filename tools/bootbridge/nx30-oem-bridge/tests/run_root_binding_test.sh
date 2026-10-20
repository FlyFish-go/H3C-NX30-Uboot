#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
out=$(mktemp)
trap 'rm -f "$out"' EXIT
cc -std=c99 -O2 -Wall -Wextra -Werror -Wconversion -Wshadow \
  -I"$root/src" "$root/tests/test_root_binding.c" \
  "$root/src/bridge_root_binding.c" -o "$out"
"$out"
