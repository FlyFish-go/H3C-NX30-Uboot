#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp_a=$(mktemp); tmp_b=$(mktemp)
trap 'rm -f "$tmp_a" "$tmp_b"' EXIT
cc -std=c11 -Wall -Wextra -Werror -I include -DCONFIG_H3C_HW_A_PRODUCTION_PORT test/httpd_upload_limit_hosttest.c -o "$tmp_a"
"$tmp_a"
cc -std=c11 -Wall -Wextra -Werror -I include test/httpd_upload_limit_hosttest.c -o "$tmp_b"
"$tmp_b"

cc -std=c11 -Wall -Wextra -Werror -I include -DRT3_INITRAMFS_HOST_TEST -DCONFIG_TARGET_IPQ5018_H3C_RT3000_B test/httpd_upload_limit_hosttest.c -o "$tmp_b"
"$tmp_b"
cc -std=c11 -Wall -Wextra -Werror -I include -DCONFIG_H3C_HW_C_RAM_READONLY test/httpd_upload_limit_hosttest.c -o "$tmp_b"
"$tmp_b"
