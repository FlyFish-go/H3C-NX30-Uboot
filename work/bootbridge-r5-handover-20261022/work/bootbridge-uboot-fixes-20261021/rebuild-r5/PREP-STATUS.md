# R5 rebuild prep status — rebuild-engineer

UTC ~2026-10-22 05:2x. NO BUILD LAUNCHED YET — release blocker found in the
designated WSL build base (see BLOCKER-BUILD-BASE.md).

## 1. mksquashfs claim — SETTLED, lead's earlier belief was WRONG
- /usr/bin/mksquashfs sha256 403080bcd98ea7be2cbb261a10e99a89571e3a3beed6ab6cc3b88e01a0b51053
- identical to the R4 pin in bundles-r4/RUN-SUMMARY.json "tools".mksquashfs
- `mksquashfs -version` => "mksquashfs version 4.5 (2021/07/22)"
- `-help` lists `-mkfs-time <time>` (alias `-fstime`) and `-all-time <time>`
- functional proof: two runs of the same tree with `-mkfs-time 1700000000
  -all-time 1700000000` produced byte-identical images
  (sha256 e22831571ee06ae9bfafe8b2c2dd2f2ca9280cbb507c91c0a7eef6acfb2645d2);
  unsquashfs -ll shows every inode at 2023-11-15 06:13 (== epoch)
- negative control: `-definitely-not-a-flag` => exit 1, "mksquashfs: invalid option"
=> build.py needs NO change for this. Nothing to fix.

## 2. Tool pins re-verified (all match R4 RUN-SUMMARY.json exactly)
- cc      c6148c70e810b199c600511a2d4c10239ee6e919da61f3d54cc92edadc3ac098
- ubinize 7eb9503535b82a7f8ad6c6d2d19016e567272e5b6341b2e100d794483015736c
- mksquashfs 403080bcd98ea7be2cbb261a10e99a89571e3a3beed6ab6cc3b88e01a0b51053
- dtc     7dc0f6b40a991e029b54dea4f5cdae15adccfa246fc156eef96ec27b525f5ea2
versions: gcc 8.4.0 (OpenWrt r16847), ubinize (mtd-utils) 2.1.2, dtc 1.6.1

## 3. Input pins re-verified (all exact)
- candidate archive 2e0972448bf39b2a37a814b725fe9514e8e7e03705ed12955c82a9cca1805008 (308700371 B)
- manifest 14a25186205e35c03a9632ecabe2492a2ee474ccde48fd33fbcd40026e6b6195
- descriptors 798aed04258f1966e2b5aee0cd459fbb5177d42efab27fdc17a38e94af7a09d9
- BUNDLE-INPUTS-v2.json 41a2e2c48ba98b01a7a0b0538f5e612309ae833854658cdcaebc31b56d9d9397
- NAND-CATALOG-REPORT-v2.json 69c0354a16e57b09621e91b23072d8dfdd646588181fe75981ea3d47e57cb055
- GUEST-GATE-CONFIG-FINAL-V2.json 6e14b2ffaa258b5445a5d4bc617f52fe7bf18b16f40dd80c1bcaf23221296aa8

## 4. Runner prerequisites confirmed
tools/build_four_sources_remote.py requires: WSL-only (platform check),
NEW absolute --output-root (must not exist), all three candidate pins,
bridge source containing build_bundle.py + audit_bundle.py, path containing
"RT3000". --jobs 1..4. Emits RUN-SUMMARY.json / RESULT.json, enforces the
4-source/17-logical-target matrix.

## 5. Identity file sync (done, verified) — but INSUFFICIENT (see blocker)
WSL tree .../rebuild-src-b0383f96/nx30-oem-bridge/src/bridge_install_identity.c
now sha256 4e68462161f1ccfbded9ecec3ab094c79b8bfbb70e36ab22c1d6ce5edeba964c
(= repo main HEAD 2bbf85a), 14669 B, policy_admits_c_candidate present (2),
e768be51 absent (0), policies[] literal rows = 4 (the frozen pre.1 records).
Backups kept: .bak-r5sync-20261022T051910Z (f058df53…, 88-row) and
.bak-pre-final-20261022T052132Z.

## NOT_EXECUTED
- No four-source build launched. Reason: BLOCKER-BUILD-BASE.md.
- No output root created. No evidence tree modified. Nothing pushed/committed.
