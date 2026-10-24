#!/bin/sh
# rt3000_mibib_hosttest.sh MIBIB_BACKUP - MIBIB structure and build identity checks.
set -eu
cd "$(dirname "$0")/.."
backup="${1:?usage: $0 MIBIB_BACKUP}"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
cc -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror \
  -DRT3_MIBIB_HOST_TEST -DH3C_BUILD_ID_HOST_TEST \
  -DCONFIG_TARGET_IPQ5018_H3C_HW_A -DCONFIG_H3C_NAND_PROFILE_ID='"h3c-oem-efba-128m-2k-sp64"' \
  -DCONFIG_H3C_NAND_SOURCE_MASK=0x1 -DCONFIG_H3C_NAND_LOAD_ECC_BITS=4 \
  -iquote include -idirafter include test/rt3000_mibib_hosttest.c failsafe/modules/rt3000_mibib.c \
  failsafe/modules/h3c_build_id.c -o "$tmp"
"$tmp" "$backup"
