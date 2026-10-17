#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
fixture="${1:?usage: $0 FIXTURE [STAGE [OEM_IMAGE]]}"
stage="${2:-u-boot.mbn}"
oem="${3:?usage: $0 FIXTURE [STAGE [OEM_IMAGE]]}"
cc -std=c99 -Wall -Wextra -Werror -DRT3_METADATA_HOST_TEST -DUSE_HOSTCC \
  -I test/rt3_metadata_stubs -iquote include -idirafter include -include stdint.h -Wno-unused-parameter \
  test/rt3000_b_metadata_core_hosttest.c \
  failsafe/modules/rt3000_b_metadata_core.c lib/sha256.c -lz -o "$tmp"
"$tmp" "$fixture" "$stage" "$oem"
