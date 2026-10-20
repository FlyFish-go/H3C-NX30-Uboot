#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 BRIDGE_SOURCE_DIR ROOT_CONTRACT_HEADER OUTPUT_PROBE" >&2
    exit 2
fi

source_dir=$(CDPATH= cd -- "$1" && pwd -P)
contract_header=$(realpath -- "$2")
output=$(realpath -m -- "$3")
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
mkdir -p "$(dirname "$output")"
cc -std=c99 -O2 -Wall -Wextra -Werror -static \
    -I "$source_dir" \
    "-DBI_ROOT_BINDING_CONTRACT_HEADER=\"$contract_header\"" \
    "$script_dir/root_binding_guest_probe.c" \
    "$source_dir/bridge_root_binding.c" -o "$output"
sha256sum "$source_dir/bridge_root_binding.c" "$output"
