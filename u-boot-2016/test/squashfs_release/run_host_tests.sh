#!/bin/sh
set -eu
TEST_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
UBOOT_DIR=$(CDPATH= cd -- "$TEST_DIR/../.." && pwd)
MKSQUASHFS=${MKSQUASHFS:-"$UBOOT_DIR/../staging_dir/host/bin/mksquashfs4"}
CC=${CC:-gcc}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
"$CC" -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
  -include /usr/include/stdlib.h -DENOTSUP=95 -DEOPNOTSUPP=95 \
  -I"$TEST_DIR/stubs" -I"$UBOOT_DIR/include" \
  -I"$UBOOT_DIR/failsafe/modules/xz" \
  "$UBOOT_DIR/failsafe/modules/squashfs_release.c" \
  "$UBOOT_DIR/failsafe/modules/xz/xz_crc32.c" \
  "$UBOOT_DIR/failsafe/modules/xz/xz_dec_stream.c" \
  "$UBOOT_DIR/failsafe/modules/xz/xz_dec_lzma2.c" \
  "$UBOOT_DIR/failsafe/modules/xz/xz_dec_bcj.c" \
  "$TEST_DIR/test_reader.c" -o "$TMP/test_reader"
python3 "$TEST_DIR/make_fixtures.py" "$TMP/fixtures"
"$MKSQUASHFS" "$TMP/fixtures/long-raw" "$TMP/long-raw.sqfs" -noappend -comp xz -b 8192 -all-root -no-fragments -noD -noI -no-xattrs -no-progress >/dev/null
"$TMP/test_reader" "$TMP/long-raw.sqfs" "OpenWrt Fixture 23.05.2 r-test123"
"$MKSQUASHFS" "$TMP/fixtures/long-regular" "$TMP/long-regular.sqfs" -noappend -comp xz -b 8192 -all-root -no-xattrs -no-progress >/dev/null
"$TMP/test_reader" "$TMP/long-regular.sqfs" "OpenWrt LongRegular 23.05.2 r-hardlink"
"$MKSQUASHFS" "$TMP/fixtures/symlink-only" "$TMP/symlink-only.sqfs" -noappend -comp xz -b 8192 -all-root -no-xattrs -no-progress >/dev/null
"$TMP/test_reader" "$TMP/symlink-only.sqfs" -ENOENT
if [ -n "${SQFS_SAMPLE:-}" ]; then
  "$TMP/test_reader" "$SQFS_SAMPLE" "OpenWrt 21.02.7 r16847-f8282da11e"
fi
