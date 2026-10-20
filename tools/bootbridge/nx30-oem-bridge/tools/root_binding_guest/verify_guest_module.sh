#!/bin/bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 MODULE EXPECTED_KERNEL_RELEASE" >&2
    exit 2
fi
module=$1
expected_release=$2
[[ -f "$module" && ! -L "$module" ]] || {
    echo "module vermagic check requires a regular module: $module" >&2
    exit 1
}
[[ -n "$expected_release" && "$expected_release" != *[[:space:]]* ]] || {
    echo "expected kernel release is invalid" >&2
    exit 2
}
command -v modinfo >/dev/null 2>&1 || {
    echo "modinfo is required for strict module release verification" >&2
    exit 2
}

check_dir=$(mktemp -d "${TMPDIR:-/tmp}/r3-modinfo.XXXXXXXX")
trap 'rm -rf -- "$check_dir"' EXIT
check_module="$check_dir/module-check.ko"
cp -- "$module" "$check_module"
vermagic=$(modinfo -F vermagic "$check_module" 2>/dev/null) || {
    echo "module metadata cannot be read: $module" >&2
    exit 1
}
[[ -n "$vermagic" && "${vermagic%% *}" == "$expected_release" ]] || {
    echo "module release mismatch: $module: $vermagic (wanted $expected_release)" >&2
    exit 1
}
