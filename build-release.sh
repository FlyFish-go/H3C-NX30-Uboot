#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Build a board-specific release ELF/DTB/MBN from this source directory.
set -euo pipefail
umask 077

if [[ $# != 2 ]]; then
  echo "Usage: CROSS_COMPILE=/absolute/toolchain/bin/arm-linux-gnueabi- $0 A|B|C|D /absolute/new-output-dir" >&2
  exit 2
fi
board="${1^^}"
out="$2"
src="$(cd -- "$(dirname -- "$0")" && pwd)"
uboot="$src/u-boot-2016"
packager="$src/release-tools/appsbl_packager.py"

case "$board" in
  A) name="ipq5018-h3c-hw-a"; config="ipq5018_h3c_hw_a_production_defconfig"; target="CONFIG_TARGET_IPQ5018_H3C_HW_A"; machid="0x08040001" ;;
  B) name="ipq5018-h3c_rt3000-b"; config="ipq5018_h3c_rt3000_b_defconfig"; target="CONFIG_TARGET_IPQ5018_H3C_RT3000_B"; machid="0x08040000" ;;
  C) name="ipq5018-h3c-hw-c"; config="ipq5018_h3c_hw_c_production_defconfig"; target="CONFIG_TARGET_IPQ5018_H3C_HW_C"; machid="0x08040101" ;;
  D) name="ipq5018-h3c-hw-d"; config="ipq5018_h3c_hw_d_production_defconfig"; target="CONFIG_TARGET_IPQ5018_H3C_HW_D"; machid="0x08040000" ;;
  *) echo "Unsupported board: $board" >&2; exit 2 ;;
esac

if [[ -z "${CROSS_COMPILE:-}" ]]; then
  echo "CROSS_COMPILE must be explicitly set to a toolchain binary prefix." >&2
  exit 2
fi
for tool in gcc ld strip nm; do
  command -v "${CROSS_COMPILE}${tool}" >/dev/null || {
    echo "Missing tool: ${CROSS_COMPILE}${tool}" >&2; exit 2;
  }
done
for tool in make python3 sha256sum; do
  command -v "$tool" >/dev/null || { echo "Missing host tool: $tool" >&2; exit 2; }
done
[[ -f "$uboot/Makefile" && -f "$uboot/configs/$config" && -f "$packager" ]] || {
  echo "Incomplete source bundle or board configuration." >&2; exit 2;
}
[[ "$out" == /* ]] || { echo "Output directory must be absolute." >&2; exit 2; }
[[ ! -e "$out" && ! -L "$out" ]] || { echo "Refusing to reuse output path: $out" >&2; exit 2; }
case "$out/" in "$src/"*) echo "Output must be outside source." >&2; exit 2;; esac

# A source archive has no .git. Pin both the recorded source identity and
# effective timestamp explicitly; neither is inferred from a local checkout.
SOURCE_REVISION="${SOURCE_REVISION:-b0383f96b3282b0aedbd9765464778a9cf596836}"
SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1792195200}"
[[ "$SOURCE_REVISION" =~ ^[0-9a-f]{40}$ && "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]] || {
  echo "Invalid SOURCE_REVISION or SOURCE_DATE_EPOCH." >&2; exit 2;
}
SOURCE_REVISION_SHORT=${SOURCE_REVISION:0:12}
export SOURCE_DATE_EPOCH TZ=UTC LC_ALL=C
# U-Boot 2016 host sysupgrade utility uses BSD strlcpy/strlcat.
# Linux glibc lacks them; use libbsd when the host libc does not provide them.
host_probe=$(mktemp -d)
trap 'rm -rf -- "$host_probe"' EXIT
cat > "$host_probe/probe.c" <<'PROBE'
#include <string.h>
int main(void) { char d[8]; return (int)strlcpy(d, "x", sizeof(d)) + (int)strlcat(d, "y", sizeof(d)); }
PROBE
if ! ${HOSTCC:-cc} "$host_probe/probe.c" -o "$host_probe/native" >/dev/null 2>&1; then
  if ${HOSTCC:-cc} "$host_probe/probe.c" -lbsd -o "$host_probe/bsd" >/dev/null 2>&1; then
    host_strl_flags=-lbsd
  elif ${HOSTCC:-cc} "$host_probe/probe.c" -l:libbsd.so.0 -o "$host_probe/bsd-soname" >/dev/null 2>&1; then
    host_strl_flags=-l:libbsd.so.0
  else
    echo "Host libc lacks strlcpy/strlcat; install libbsd development/runtime files or use a libc providing BSD string functions." >&2
    exit 2
  fi
else
  host_strl_flags=
fi
rm -rf -- "$host_probe"
trap - EXIT
host_make_args=()
if [[ -n "$host_strl_flags" ]]; then
  host_make_args=("HOST_LOADLIBES=${HOST_LOADLIBES:+$HOST_LOADLIBES }$host_strl_flags")
fi
# The archive has no .git. Pass the reviewed source revision explicitly to Kbuild.
host_make_args+=("GIT_HASH=$SOURCE_REVISION_SHORT" "GIT_DIRTY=0" "GIT_VERSION=$SOURCE_REVISION_SHORT")
export ARCH=arm CROSS_COMPILE
export KBUILD_BUILD_VERSION=1 KBUILD_BUILD_USER=source-build KBUILD_BUILD_HOST=portable
export KBUILD_BUILD_TIMESTAMP="$(date -u -d "@$SOURCE_DATE_EPOCH" '+%a %b %d %H:%M:%S UTC %Y')"

mkdir -m 0700 -- "$out"
out="$(cd -- "$out" && pwd)"
build="$out/u-boot-build"
export PATH="$(dirname -- "$(command -v "${CROSS_COMPILE}gcc")"):$PATH"

make "${host_make_args[@]}" -C "$uboot" O="$build" "$config"
# Explicit release profile. Keep production defaults otherwise unchanged.
sed -i '/^# CONFIG_H3C_RELEASE_SYSTEM_DEFAULT is not set$/d; /^CONFIG_H3C_RELEASE_SYSTEM_DEFAULT=/d' "$build/.config"
printf '%s\n' 'CONFIG_H3C_RELEASE_SYSTEM_DEFAULT=y' >> "$build/.config"
# A profile pin selects a reviewed OEM NAND format; it never converts ECC.
if [[ -n "${H3C_NAND_PROFILE_ID:-}" ]]; then
  [[ "$H3C_NAND_PROFILE_ID" =~ ^h3c-oem-[a-z0-9-]+$ ]] || exit 2
  [[ "${H3C_NAND_SOURCE_MASK:-}" =~ ^(1|2|4|8|16)$ ]] || exit 2
  [[ "${H3C_NAND_LOAD_ECC_BITS:-}" =~ ^(4|8)$ ]] || exit 2
  [[ "${H3C_NAND_STORAGE_ECC_BITS:-}" == "$H3C_NAND_LOAD_ECC_BITS" ]] || exit 2
  sed -i '/^CONFIG_H3C_NAND_PROFILE_ID=/d; /^CONFIG_H3C_NAND_SOURCE_MASK=/d; /^CONFIG_H3C_NAND_LOAD_ECC_BITS=/d; /^CONFIG_H3C_NAND_STORAGE_ECC_BITS=/d' "$build/.config"
  printf 'CONFIG_H3C_NAND_PROFILE_ID="%s"\nCONFIG_H3C_NAND_SOURCE_MASK=0x%x\nCONFIG_H3C_NAND_LOAD_ECC_BITS=%d\nCONFIG_H3C_NAND_STORAGE_ECC_BITS=%d\n' "$H3C_NAND_PROFILE_ID" "$H3C_NAND_SOURCE_MASK" "$H3C_NAND_LOAD_ECC_BITS" "$H3C_NAND_STORAGE_ECC_BITS" >> "$build/.config"
fi
# A RAM rescue image runs below an installed U-Boot and is loaded with bootelf.
text_base=0x4A920000
if [[ "${H3C_RAM_RESCUE:-0}" == 1 ]]; then
  text_base=0x49A20000
  sed -i '/^# CONFIG_H3C_RAM_RESCUE is not set$/d; /^CONFIG_H3C_RAM_RESCUE=/d' "$build/.config"
  printf '%s\n' 'CONFIG_H3C_RAM_RESCUE=y' >> "$build/.config"
fi
make "${host_make_args[@]}" -C "$uboot" O="$build" olddefconfig
[[ "${H3C_RAM_RESCUE:-0}" != 1 ]] || grep -Fxq 'CONFIG_H3C_RAM_RESCUE=y' "$build/.config"
grep -Fxq 'CONFIG_H3C_RELEASE_SYSTEM_DEFAULT=y' "$build/.config"
grep -Fxq "$target=y" "$build/.config"
grep -Fxq 'CONFIG_H3C_IPQ5018_COMMON=y' "$build/.config"
grep -Fq "CONFIG_DEFAULT_DEVICE_TREE=\"$name\"" "$build/.config"
grep -Fq "$name" "$uboot/arch/arm/dts/Makefile"

make "${host_make_args[@]}" -C "$uboot" O="$build" -j1 LDFLAGS_u-boot="-pie --gc-sections --nmagic -Ttext $text_base"
if [[ "$board" == D ]]; then
  for symbol in h3c_release_autoboot h3c_boot_recovery h3c_boot_decide h3c_boot_sample_reset h3c_boot_load_failed; do
    "${CROSS_COMPILE}nm" "$build/u-boot" | awk -v s="$symbol" '$3 == s { found=1 } END { exit !found }'
  done
fi

cp -- "$build/dtb_combined.bin" "$out/dtb_combined.bin"
cp -- "$build/dtb_combined.bin.gz" "$out/dtb_combined.bin.gz"
python3 - "$out/dtb_combined.bin" "$out/$name.packed.dtb" "$machid" "$text_base" <<'PYDTB'
import struct, sys
from pathlib import Path
blob = Path(sys.argv[1]).read_bytes()
if len(blob) < 12 or struct.unpack_from("<I", blob, 0)[0] != 1:
    raise SystemExit("combined DTB must contain exactly one table entry")
machid, address = struct.unpack_from("<II", blob, 4)
offset = address - (int(sys.argv[4], 0) - 0x40000)
if machid != int(sys.argv[3], 0) or offset < 12 or offset + 40 > len(blob):
    raise SystemExit("combined DTB table has an invalid board entry")
if struct.unpack_from(">I", blob, offset)[0] != 0xD00DFEED:
    raise SystemExit("combined DTB entry does not point to FDT")
size = struct.unpack_from(">I", blob, offset + 4)[0]
if size < 40 or offset + size > len(blob):
    raise SystemExit("combined DTB FDT extent is invalid")
Path(sys.argv[2]).write_bytes(blob[offset:offset + size])
PYDTB

label="${board,,}"
stem="h3c-hw-$label-common-release"
images=(appsbl.mbn appsbl.partition.bin)
profile=release
if [[ "${H3C_RAM_RESCUE:-0}" == 1 ]]; then
  stem="h3c-hw-$label-ram-rescue"; images=(); profile=ram-rescue
fi
cp -- "$build/u-boot" "$out/$stem.elf"
"${CROSS_COMPILE}strip" "$out/$stem.elf" -o "$out/$stem.strip.elf"
cp -- "$build/.config" "$out/build.config"
# The APPSBL packager only accepts the flash link address.
[[ "$profile" == ram-rescue ]] || python3 "$packager" "$out/$stem.strip.elf" "$out/appsbl.mbn" \
  --partition "$out/appsbl.partition.bin" --board "$board"
"${CROSS_COMPILE}gcc" --version > "$out/toolchain.txt"
"${CROSS_COMPILE}ld" --version >> "$out/toolchain.txt"
cat > "$out/provenance.json" <<EOF
{
  "schema": 1,
  "source_revision": "$SOURCE_REVISION",
  "embedded_git_revision": "$SOURCE_REVISION_SHORT",
  "embedded_git_dirty": false,
  "source_date_epoch": "$SOURCE_DATE_EPOCH",
  "source_tree_mode": "source snapshot identifier; no Git commit or hardware qualification implied",
  "board": "$board",
  "profile": "$profile",
  "hardware_validation": "NOT_EXECUTED",
  "installation_status": "offline candidate only"
}
EOF
(cd "$out" && sha256sum "$stem.elf" "$stem.strip.elf" "$name.packed.dtb" "${images[@]}" build.config toolchain.txt provenance.json > SHA256SUMS)
echo "Offline candidate built. This does not establish hardware or installation qualification."
