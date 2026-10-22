#!/bin/bash
# Prove which build.py branch the neutral-archive path takes, and what the
# resulting provider does or does not carry. Runs entirely in a private /tmp copy.
set -u
W=/home/yu/Project/RT3000
SRC=$W/evidence/nand-oem-repair-20261017/rebuild-src-b0383f96/nx30-oem-bridge
PRIV=/tmp/va-neutral/$$/src
OUT=/tmp/va-neutral/$$/out
CC=$W/RT3000-QSDK11.5/common/openwrt-redmi-ax3000/staging_dir/toolchain-arm_cortex-a7+neon-vfpv4_gcc-8.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-gcc
UBI=$W/RT3000-QSDK11.5/common/openwrt-redmi-ax3000/staging_dir/host/bin/ubinize
mkdir -p "$(dirname "$PRIV")"
cp -a "$SRC" "$PRIV" || exit 90
# Patch the private copy's identity TU to HEAD 2bbf85a, exactly as R5 will.
cp /tmp/va-head-2bbf85a.c \
   "$PRIV/src/bridge_install_identity.c" || exit 91
echo "== private src prepared"
sha256sum "$PRIV/src/bridge_install_identity.c" "$PRIV/tools/build.py"
echo "== tool availability"
for t in "$CC" "$UBI" /usr/bin/mksquashfs /usr/bin/dtc; do
    [ -x "$t" ] && echo "OK   $t" || echo "MISS $t"
done
echo "== neutral-archive build (NO candidate_descriptor; direct build.py CLI)"
mkdir -p "$OUT"
cd "$PRIV" || exit 92
python3 tools/build.py \
  --official "$W/evidence/bootbridge-resume-20261016/gui-export-inputs/RC3000-original.bin" \
  --rootfs "$W/evidence/bootbridge-goal-20261015/multisource-fullbuild-lab-20261015/inputs/RC3000/rootfs" \
  --output "$OUT" \
  --cc "$CC" --ubinize "$UBI" --mksquashfs /usr/bin/mksquashfs --dtc /usr/bin/dtc \
  --candidate-archive "$W/evidence/bridge10-remap-20261020/nand-uboot-candidates-b0383f96b328-v2.tar.gz" \
  --hardware-class C \
  --payload-report "$W/evidence/nand-oem-repair-20261017/inputs/NAND-CATALOG-REPORT-v2.json" \
  > /tmp/va-neutral/$$/build.log 2>&1
RC=$?
echo "build.py exit=$RC"
echo "== generated policy header written?"
find "$OUT" -name 'bii_candidate_policy.generated.h' -print 2>/dev/null | sed 's/^/  /'
[ -z "$(find "$OUT" -name 'bii_candidate_policy.generated.h' -print -quit 2>/dev/null)" ] && echo "  (none written)"
echo "== provider binary: does it carry its own R4 digest?"
PROV=$(find "$OUT" -path '*bridge-candidates*' -name provider -type f 2>/dev/null | head -1)
echo "  provider=$PROV"
if [ -n "$PROV" ]; then
    for d in 63f6538e7431b1b9a3c208f19844296bad605fb44546369a6495d7ec5725efc0 \
             57d13d1dad2a7b7854799b7cc69bddec638dd61df70f1b75a4fd94650fc96d8e; do
        n=$(grep -c "$d" "$PROV" 2>/dev/null || true)
        echo "  digest $d occurrences=$n"
    done
    echo "  machid 08040101 occurrences=$(grep -c 08040101 "$PROV" 2>/dev/null || true)"
fi
echo "== tail of build log"
tail -20 /tmp/va-neutral/$$/build.log
