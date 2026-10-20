#!/bin/bash
set -euo pipefail

if [[ $# -ne 8 ]]; then
    echo "usage: $0 KERNEL_IMAGE BASE_INITRD KERNEL_RELEASE MODULE_ROOT BRIDGE_SOURCE_DIR CONTRACT_HEADER BRIDGE_UBI OUTPUT_DIR" >&2
    exit 2
fi
kernel=$(realpath -e -- "$1")
base_initrd=$(realpath -e -- "$2")
kernel_release=$3
module_root=$(realpath -e -- "$4")
source_dir=$(realpath -e -- "$5")
contract=$(realpath -e -- "$6")
image=$(realpath -e -- "$7")
out=$(realpath -m -- "$8")
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)

[[ -f "$kernel" && ! -L "$kernel" && -f "$base_initrd" && ! -L "$base_initrd" ]] || {
    echo "kernel/initrd must be regular files" >&2; exit 2;
}
[[ -f "$image" && ! -L "$image" && -s "$image" ]] || {
    echo "bridge UBI must be a nonempty regular file" >&2; exit 2;
}
[[ -f "$source_dir/bridge_root_binding.c" && -f "$source_dir/bridge_root_binding.h" ]] || {
    echo "bridge source directory lacks root-binding validator" >&2; exit 2;
}
[[ -f "$contract" && ! -L "$contract" ]] || { echo "contract must be a regular generated header" >&2; exit 2; }
mkdir -p "$out"
out=$(realpath -e -- "$out")
tmp=$(mktemp -d "$out/.capsule.XXXXXXXX")
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/overlay/r3/modules" "$tmp/overlay/r3/input" "$tmp/overlay/r3/bin"

module_src="$module_root/kernel/drivers/mtd"
copy_module() {
    local rel=$1 name=$2 src="$module_root/kernel/$1"
    [[ -f "$src" && ! -L "$src" ]] || { echo "missing matching module: $src" >&2; exit 1; }
    xz -dc -- "$src" >"$tmp/overlay/r3/modules/$name.ko"
}
copy_module drivers/mtd/mtd.ko.xz mtd
copy_module lib/bch.ko.xz bch
copy_module drivers/mtd/nand/nandcore.ko.xz nandcore
copy_module drivers/mtd/nand/raw/nand.ko.xz nand
copy_module drivers/mtd/nand/raw/nandsim.ko.xz nandsim
copy_module drivers/mtd/ubi/ubi.ko.xz ubi
copy_module fs/squashfs/squashfs.ko.xz squashfs
[[ -f "$script_dir/guest-modules/modules.json" ]] || {
    echo "pinned guest module receipt is missing" >&2; exit 1;
}
for module in gluebi_guest.ko r3_mtd_slots.ko mtd_blkdevs_guest.ko mtdblock_guest.ko; do
    [[ -f "$script_dir/guest-modules/$module" && ! -L "$script_dir/guest-modules/$module" ]] || {
        echo "pinned guest module is missing: $module" >&2; exit 1;
    }
    cp -- "$script_dir/guest-modules/$module" "$tmp/overlay/r3/modules/"
done
python3 - "$script_dir/guest-modules/modules.json" "$tmp/overlay/r3/modules" "$kernel_release" <<'PY'
import hashlib, json, pathlib, sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
root = pathlib.Path(sys.argv[2])
release = sys.argv[3]
assert manifest.get("schema") == "r3-guest-module-set-v1" and manifest.get("status") == "BUILT"
assert manifest.get("kernel_release") == release
for name, pin in manifest.get("modules", {}).items():
    data = (root / name).read_bytes()
    assert len(data) == pin.get("bytes")
    assert hashlib.sha256(data).hexdigest() == pin.get("sha256")
assert set(manifest["modules"]) == {
    "gluebi_guest.ko", "r3_mtd_slots.ko", "mtd_blkdevs_guest.ko", "mtdblock_guest.ko"
}
PY
for module in "$tmp/overlay/r3/modules/"*.ko; do
    "$script_dir/verify_guest_module.sh" "$module" "$kernel_release"
done

"$script_dir/build_guest_probe.sh" "$source_dir" "$contract" "$tmp/overlay/r3/probe" >"$tmp/probe-build.txt"
cp -- "$image" "$tmp/overlay/r3/input/bridge.ubi"
image_sha=$(sha256sum "$image" | awk '{print $1}')
copied_image_sha=$(sha256sum "$tmp/overlay/r3/input/bridge.ubi" | awk '{print $1}')
[[ "$copied_image_sha" == "$image_sha" ]] || { echo "staged bridge UBI hash mismatch" >&2; exit 1; }
printf '%s\n' "$image_sha" >"$tmp/overlay/r3/image.sha256"
PYTHONPATH="$source_dir/../tools${PYTHONPATH:+:$PYTHONPATH}" python3 - "$image" "$contract" "$tmp/overlay/r3/input/ubi-vid-offset" "$tmp/ubi-layout.json" <<'PY'
import json, pathlib, re, sys
import vendor_ubi
image, contract_header, offset_path, layout_path = map(pathlib.Path, sys.argv[1:])
header = contract_header.read_text(encoding="ascii")
m = re.search(r"#define BI_ROOT_BINDING_CONTRACT \{ UINT64_C\((\d+)\), UINT64_C\((\d+)\), UINT64_C\((\d+)\), UINT64_C\((\d+)\) \}", header)
if not m:
    raise SystemExit("generated root binding macro does not match expected contract shape")
contract = tuple(map(int, m.groups()))
meta, _, _, _ = vendor_ubi.parse(image)
peb = image.stat().st_size // meta["total_pebs"]
rows = [row for row in meta["rows"] if row.get("kind") == "EC_VID"]
geometry = {(row.get("vid_offset"), row.get("data_offset")) for row in rows}
volumes = [row for row in meta["volume_table"] if row.get("name") == "ubi_rootfs"]
if len(geometry) != 1 or len(volumes) != 1:
    raise SystemExit("built UBI image has ambiguous root volume or EC/VID geometry")
vid, data = next(iter(geometry))
root = volumes[0]
actual = (root.get("capacity"), peb - data, root.get("reserved_pebs"), data - vid)
if root.get("id") != 3 or root.get("type") != "dynamic" or actual != contract:
    raise SystemExit(f"built UBI root contract mismatch: image={actual!r} header={contract!r}")
if peb != 131072 or data % 2048 or vid % 2048 or not rows:
    raise SystemExit("built UBI PEB/VID geometry is not supported by this reviewed guest")
offset_path.write_text(str(vid) + "\n", encoding="ascii")
with layout_path.open("w", encoding="utf-8") as f:
    json.dump({"peb_bytes": peb, "vid_header_offset": vid, "data_offset": data,
               "root_volume_id": root["id"], "root_volume_name": root["name"],
               "root_volume_capacity": root["capacity"], "root_volume_reserved_ebs": root["reserved_pebs"],
               "source_contract_matches_built_image": True}, f, indent=2, sort_keys=True)
    f.write("\n")
PY
cp -- "$script_dir/guest-init" "$tmp/overlay/init"
cc -std=c99 -O2 -Wall -Wextra -Werror -static \
    "$script_dir/guest_mount_root.c" -o "$tmp/overlay/r3/bin/mount-root"
chmod 0755 "$tmp/overlay/init" "$tmp/overlay/r3/probe"
cp -- "$script_dir/guest-tools/usr/sbin/nandwrite" "$tmp/overlay/r3/bin/nandwrite"
cp -- "$script_dir/guest-tools/usr/sbin/ubiattach" "$tmp/overlay/r3/bin/ubiattach"
chmod 0755 "$tmp/overlay/r3/bin/"*

mkdir -p "$tmp/overlay/dev" "$tmp/overlay/proc" "$tmp/overlay/sys" "$tmp/overlay/newroot"
(
    cd "$tmp/overlay"
    find . -exec touch -h -d '@0' -- {} +
    find . -print0 | sort -z | cpio --null --create --format=newc --owner=0:0 --reproducible --quiet | gzip -n -9
) >"$tmp/overlay.cpio.gz"
cat "$base_initrd" "$tmp/overlay.cpio.gz" >"$tmp/initrd.img"

image_sha=$(sha256sum "$image" | awk '{print $1}')
contract_sha=$(sha256sum "$contract" | awk '{print $1}')
kernel_sha=$(sha256sum "$kernel" | awk '{print $1}')
base_initrd_sha=$(sha256sum "$base_initrd" | awk '{print $1}')
capsule_initrd_sha=$(sha256sum "$tmp/initrd.img" | awk '{print $1}')
probe_sha=$(sha256sum "$tmp/overlay/r3/probe" | awk '{print $1}')
python3 - "$out/capsule.json" "$kernel_release" "$image" "$image_sha" "$contract" "$contract_sha" "$kernel" "$kernel_sha" "$base_initrd" "$base_initrd_sha" "$capsule_initrd_sha" "$probe_sha" "$tmp/ubi-layout.json" "$script_dir/guest-modules/modules.json" "$script_dir/guest_mount_root.c" <<'PY'
import json, os, sys
(out, release, image, image_sha, contract, contract_sha, kernel, kernel_sha,
 base_initrd, base_initrd_sha, capsule_initrd_sha, probe_sha, layout_path,
 modules_path, mount_source) = sys.argv[1:]
sha = lambda p: __import__("hashlib").sha256(open(p, "rb").read()).hexdigest()
obj = {
    "schema": "r3-live-gluebi-qemu-capsule-v1",
    "status": "PREPARED",
    "kernel_release": release,
    "kernel": {"path": os.path.realpath(kernel), "sha256": kernel_sha},
    "base_initrd": {"path": os.path.realpath(base_initrd), "sha256": base_initrd_sha},
    "capsule_initrd_sha256": capsule_initrd_sha,
    "bridge_ubi": {"path": os.path.realpath(image), "sha256": image_sha, "bytes": os.path.getsize(image)},
    "generated_contract_header": {"path": os.path.realpath(contract), "sha256": contract_sha},
    "production_probe_sha256": probe_sha,
    "guest_modules": json.load(open(modules_path, encoding="utf-8")),
    "guest_transition_source": {"path": os.path.realpath(mount_source), "sha256": sha(mount_source)},
    "ubi_layout": json.load(open(layout_path, encoding="utf-8")),
    "guest_runtime": "guest-init loads matching nandsim/UBI/GlueBI kernel modules, writes only guest nandsim, mounts the actual ubi_rootfs SquashFS and execs the production validator probe",
}
with open(out, "w", encoding="utf-8") as f:
    json.dump(obj, f, indent=2, sort_keys=True)
    f.write("\n")
PY
install -m 0644 "$kernel" "$out/vmlinuz"
install -m 0644 "$tmp/initrd.img" "$out/initrd.img"
[[ "$(sha256sum "$out/vmlinuz" | awk '{print $1}')" == "$kernel_sha" ]] || { echo "staged kernel hash mismatch" >&2; exit 1; }
[[ "$(sha256sum "$out/initrd.img" | awk '{print $1}')" == "$capsule_initrd_sha" ]] || { echo "staged initrd hash mismatch" >&2; exit 1; }
echo "CAPSULE_READY image_sha256=$image_sha initrd_sha256=$capsule_initrd_sha probe_sha256=$probe_sha"
