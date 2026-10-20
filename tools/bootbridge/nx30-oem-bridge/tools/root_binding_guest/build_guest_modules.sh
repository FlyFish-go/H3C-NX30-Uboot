#!/bin/sh
set -eu

if [ "$#" -ne 3 ]; then
    echo "usage: $0 KERNEL_BUILD_DIR MODULE_SOURCE_DIR OUTPUT_DIR" >&2
    exit 2
fi

kernel_dir=$(realpath -- "$1")
module_dir=$(realpath -- "$2")
output_dir=$(realpath -m -- "$3")
release=$(make -s -C "$kernel_dir" kernelrelease)
expected_release=6.12.111+deb13-amd64
if [ "$release" != "$expected_release" ]; then
    echo "kernel headers mismatch: expected $expected_release, got $release" >&2
    exit 1
fi
grep -qx 'CONFIG_MTD_NAND_NANDSIM=m' "$kernel_dir/.config"
grep -qx 'CONFIG_MTD_UBI=m' "$kernel_dir/.config"

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
compat_dir="$script_dir/mtdblock-compat"

gluebi_sha=bf6fd7aafddeab637180c5681dfbd5add4c27aac91c979d85831d5a8c6640d01
media_sha=488e37c536430db0137527dd051c426a0383d47d7156a5b5bf2667c8b876a018
echo "$gluebi_sha  $module_dir/gluebi.c" | sha256sum -c -
echo "$media_sha  $module_dir/ubi-media.h" | sha256sum -c -

mkdir -p "$output_dir"
make -C "$kernel_dir" M="$module_dir" CONFIG_DEBUG_INFO_BTF_MODULES= modules
for module in gluebi_guest.ko r3_mtd_slots.ko; do
    test -f "$module_dir/$module"
    cp -- "$module_dir/$module" "$output_dir/$module"
    vermagic=$(strings "$output_dir/$module" | sed -n 's/^vermagic=//p')
    case "$vermagic" in
        "$expected_release"*) ;;
        *) echo "module $module has unexpected vermagic: $vermagic" >&2; exit 1 ;;
    esac
done
strings "$output_dir/gluebi_guest.ko" | grep -qx 'depends=ubi,mtd'

# Linux 6.12 intentionally excludes MTD_UBIVOLUME devices from mtdblock.
# Build the exact upstream translation layer with only those two exclusions
# removed, alongside the unchanged mtdblock consumer, for this disposable VM.
test -f "$compat_dir/Makefile"
test "$(sha256sum "$compat_dir/mtd_blkdevs.c" | awk '{print $1}')" = \
    fdffda75991475b30a13fd192d39958282584a6aabb22c6198f75b5ce16e4412
test "$(sha256sum "$compat_dir/mtdblock.c" | awk '{print $1}')" = \
    7e1c53b3e3229d8fbc0a167336d4d59e437c58b784f6b50d4e8e610ba5505128
test "$(sha256sum "$compat_dir/mtdcore.h" | awk '{print $1}')" = \
    98e980cd635f40ece5eb1bbb78d292741d5b007fc95ead8380477de93c8fd805
make -C "$kernel_dir" M="$compat_dir" CONFIG_DEBUG_INFO_BTF_MODULES= modules
for module in mtd_blkdevs_guest.ko mtdblock_guest.ko; do
    test -f "$compat_dir/$module"
    cp -- "$compat_dir/$module" "$output_dir/$module"
    vermagic=$(strings "$output_dir/$module" | sed -n 's/^vermagic=//p')
    case "$vermagic" in
        "$expected_release"*) ;;
        *) echo "module $module has unexpected vermagic: $vermagic" >&2; exit 1 ;;
    esac
done
strings "$output_dir/mtd_blkdevs_guest.ko" | grep -qx 'depends=mtd'
strings "$output_dir/mtdblock_guest.ko" | grep -qx 'depends=mtd,mtd_blkdevs_guest'
sha256sum "$module_dir/gluebi.c" "$module_dir/ubi-media.h" \
    "$output_dir/gluebi_guest.ko" "$output_dir/r3_mtd_slots.ko" \
    "$compat_dir/mtd_blkdevs.c" "$compat_dir/mtdblock.c" "$compat_dir/mtdcore.h" \
    "$output_dir/mtd_blkdevs_guest.ko" "$output_dir/mtdblock_guest.ko"
python3 - "$kernel_dir" "$module_dir" "$compat_dir" "$output_dir/modules.json" "$expected_release" <<'PY'
import hashlib, json, pathlib, sys
kernel, gluebi_dir, compat_dir, receipt, release = map(pathlib.Path, sys.argv[1:])
def pin(path):
    return {"sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "bytes": path.stat().st_size}
sources = {
    "gluebi.c": pin(gluebi_dir / "gluebi.c"),
    "ubi-media.h": pin(gluebi_dir / "ubi-media.h"),
    "r3_mtd_slots.c": pin(gluebi_dir / "r3_mtd_slots.c"),
    "gluebi-module/Makefile": pin(gluebi_dir / "Makefile"),
    "mtdblock-compat/mtd_blkdevs.c": pin(compat_dir / "mtd_blkdevs.c"),
    "mtdblock-compat/mtdblock.c": pin(compat_dir / "mtdblock.c"),
    "mtdblock-compat/mtdcore.h": pin(compat_dir / "mtdcore.h"),
    "mtdblock-compat/Makefile": pin(compat_dir / "Makefile"),
    "mtdblock-compat/SOURCE-PROVENANCE.json": pin(compat_dir / "SOURCE-PROVENANCE.json"),
}
modules = {}
for name in ("gluebi_guest.ko", "r3_mtd_slots.ko", "mtd_blkdevs_guest.ko", "mtdblock_guest.ko"):
    modules[name] = pin(pathlib.Path(receipt).parent / name)
obj = {
    "schema": "r3-guest-module-set-v1",
    "status": "BUILT",
    "kernel_release": str(release),
    "kernel_build_inputs": {"config_sha256": pin(kernel / ".config")["sha256"],
                             "module_symvers_sha256": pin(kernel / "Module.symvers")["sha256"]},
    "modules": modules,
    "sources": sources,
    "variance": "guest-only-mtdblock-registration-compatibility-for-MTD_UBIVOLUME; production validator unchanged",
}
pathlib.Path(receipt).write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n", encoding="utf-8")
PY
