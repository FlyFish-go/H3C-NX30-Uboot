#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 SQUASHFS_MODULE_XZ EXPECTED_KERNEL_RELEASE" >&2
    exit 2
fi
module_xz=$1
release=$2
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd -P)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/r3-module-vermagic-test.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
xz -dc -- "$module_xz" >"$tmp/squashfs.ko"

# This exact squashfs basename collides with installed module metadata on some
# hosts. The production helper must inspect these bytes under a neutral name.
"$script_dir/verify_guest_module.sh" "$tmp/squashfs.ko" "$release"
if "$script_dir/verify_guest_module.sh" "$tmp/squashfs.ko" "${release}.wrong"; then
    echo "wrong expected release was accepted" >&2
    exit 1
fi

python3 - "$tmp/squashfs.ko" "$tmp/wrong-release.ko" "$release" <<'PY'
import pathlib, sys
source, target = map(pathlib.Path, sys.argv[1:3])
release = sys.argv[3].encode("ascii")
data = source.read_bytes()
needle = b"vermagic=" + release
if data.count(needle) != 1:
    raise SystemExit("fixture does not have one expected vermagic field")
replacement = bytearray(release)
replacement[-1] = ord("0") if replacement[-1] != ord("0") else ord("1")
target.write_bytes(data.replace(needle, b"vermagic=" + replacement, 1))
PY
if "$script_dir/verify_guest_module.sh" "$tmp/wrong-release.ko" "$release"; then
    echo "module with modified vermagic was accepted" >&2
    exit 1
fi

printf 'not an ELF kernel module\n' >"$tmp/corrupt.ko"
if "$script_dir/verify_guest_module.sh" "$tmp/corrupt.ko" "$release"; then
    echo "corrupt module was accepted" >&2
    exit 1
fi
echo "VERIFY_GUEST_MODULE_TEST PASS"
