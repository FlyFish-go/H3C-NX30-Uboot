#!/bin/bash
# Neutral-archive path with the ONLY archive it accepts (the frozen ABCD one).
# Tests whether the resulting provider carries its own candidate row.
set -u
W=/home/yu/Project/RT3000
SRCROOT=$W/evidence/nand-oem-repair-20261017/rebuild-src-b0383f96
SRC=$SRCROOT/nx30-oem-bridge
BASE=/tmp/va-neutral2/$$
PRIV=$BASE/src
OUT=$BASE/out
CC=$W/RT3000-QSDK11.5/common/openwrt-redmi-ax3000/staging_dir/toolchain-arm_cortex-a7+neon-vfpv4_gcc-8.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-gcc
UBI=$W/RT3000-QSDK11.5/common/openwrt-redmi-ax3000/staging_dir/host/bin/ubinize
mkdir -p "$BASE"
# build.py resolves PROJECT as parents[1] and nand_schema expects
# <root>/u-boot-2016/include/h3c_nand_catalog.h, so copy the whole source root.
cp -a "$SRCROOT" "$BASE/root" || exit 90
PRIV=$BASE/root/nx30-oem-bridge
cp /tmp/va-head-2bbf85a.c "$PRIV/src/bridge_install_identity.c" || exit 91
echo "== identity TU sha: $(sha256sum "$PRIV/src/bridge_install_identity.c" | cut -d' ' -f1)"
# build.py requires a non-existent output dir
cd "$PRIV" || exit 92
echo "== neutral-archive build with the frozen ABCD archive"
python3 tools/build.py \
  --official "$W/evidence/bootbridge-resume-20261016/gui-export-inputs/RC3000-original.bin" \
  --rootfs "$W/evidence/bootbridge-goal-20261015/multisource-fullbuild-lab-20261015/inputs/RC3000/rootfs" \
  --output "$OUT" \
  --cc "$CC" --ubinize "$UBI" --mksquashfs /usr/bin/mksquashfs --dtc /usr/bin/dtc \
  --candidate-archive "$W/evidence/abcd-release-20261016/public-source-review/abcd-uboot-public-review-20261016.tar.gz" \
  --hardware-class C \
  --payload-report "$W/evidence/nand-oem-repair-20261017/inputs/NAND-CATALOG-REPORT-v2.json" \
  > "$BASE/build.log" 2>&1
RC=$?
echo "build.py exit=$RC"
echo "== generated policy header written?"
find "$OUT" -name 'bii_candidate_policy.generated.h' -print 2>/dev/null | sed 's/^/  /'
if [ -z "$(find "$OUT" -name 'bii_candidate_policy.generated.h' -print -quit 2>/dev/null)" ]; then
    echo "  NONE WRITTEN"
fi
echo "== provider binary"
PROV=$(find "$OUT" -path '*bridge-candidates*' -name provider -type f 2>/dev/null | head -1)
echo "  provider=$PROV"
if [ -n "$PROV" ]; then
    for d in 57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e \
             63f6538e7431b1b9a3c208f19844296bad605fb44546369a6495d7ec5725efc0; do
        echo "  digest $d occurrences=$(grep -c "$d" "$PROV" 2>/dev/null || echo 0)"
    done
    echo "  machid 08040101 occurrences=$(grep -c 08040101 "$PROV" 2>/dev/null || echo 0)"
fi
echo "== build log tail"
tail -25 "$BASE/build.log"
