#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
cc -std=c99 -Wall -Wextra -Werror -DRT3_METADATA_HOST_TEST -DRT3_METADATA_HOST_TEST_C -DUSE_HOSTCC -I test/rt3_metadata_stubs -iquote include -idirafter include -include stdint.h -Wno-unused-parameter test/hw_c_metadata_identity_hosttest.c failsafe/modules/rt3000_b_metadata_core.c lib/sha256.c -lz -o "$tmp"
"$tmp"
