#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
fixture="${1:?usage: $0 FIXTURE [STAGE]}"
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wno-unused-parameter \
  -DRT3_METADATA_HOST_TEST -DUSE_HOSTCC -include stdint.h -Wno-unused-parameter \
  -I test/metadata_writer_stubs -I test/rt3_metadata_stubs \
  -iquote include -idirafter include \
  test/metadata_writer_stubs/metadata_writer_hosttest.c \
  failsafe/modules/metadata_rt3000_b.c \
  failsafe/modules/rt3000_b_metadata_core.c failsafe/modules/rt3000_mibib.c \
  -DRT3_MIBIB_HOST_TEST lib/sha256.c -lz -o "$tmp"
if [ "$#" -gt 1 ]; then
  "$tmp" "$fixture" "$2"
else
  "$tmp" "$fixture"
fi
