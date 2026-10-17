# Building the HW-A/B/C/D U-Boot source bundle

This directory is a source-review candidate made from U-Boot tree commit
`5f486f5f9852c26d7d16eb5980a06cccea2a5217`, with a small, documented identity
sanitization overlay. The source archive is intended to build without a `.git`
directory. `build-release.sh` writes into a new caller-selected output path and
does not select a fixed `/tmp` checkout or build directory.

## Inputs

Host requirements are Bash, GNU make, Python 3, GNU `sha256sum`, GNU `date`,
and a working host C compiler, linker, zlib development headers, and the usual
U-Boot 2016 host build tools. A suitable ARM cross-toolchain must be supplied
separately. No toolchain is included in this source bundle.

The toolchain binary prefix must be explicitly provided. For example:

```sh
CROSS_COMPILE=/opt/toolchains/arm-openwrt-linux- \
  ./build-release.sh D /absolute/path/to/new-output
```

The supplied prefix must resolve `gcc`, `ld`, `strip`, and `nm`. The builder
uses the board release defconfig, `ARCH=arm`, and a separate `O=` build tree
under the new output directory. It emits the unstripped and stripped ELF,
combined and extracted DTB, normalized MBN, and the `0xff`-padded APPSBL
partition image. The MBN conversion is performed by the accompanying
independently implemented Python packager; Qualcomm's proprietary
`elftombn.py` / `mbn_tools.py` are not included or required.

`SOURCE_REVISION` and `SOURCE_DATE_EPOCH` default to the frozen source commit
and its recorded effective epoch. Its first 12 hex digits are passed to Kbuild
as GIT_HASH/GIT_VERSION because the archive has no .git; full revision and
epoch are written to provenance.json. This embeds the reviewed revision, but
does not make arbitrary-path builds byte-identical to the frozen candidate.

## Limits and portability

The known canonical artifact build used OpenWrt GCC 5.2.0 and binutils 2.24
from a private tracked `staging_dir`. That directory contains about 738 MB of
toolchain/staging files and is deliberately excluded. The exact toolchain has
no approved public download URL plus content hash in this source-review
bundle. A caller-supplied compiler may therefore produce different bytes.

The old reproducibility procedure used a fixed source/build path because that
GCC embeds absolute source paths and does not support the needed macro path
mapping. The four-board build was run with the frozen GCC 5.2.0/binutils 2.24
toolchain. Its normalized MBN and padded partition hashes differ from the
canonical candidate on all boards. The stripped ELF size deltas are A +896,
B +832, C +896, and D +896 bytes. The portable and canonical ELF files embed
different source locations; the A file contains its portable source path 17
times. No arbitrary-path bit-reproducibility claim is made. See the archive-root `BUILD-RESULTS.md` for candidate hashes and canonical comparisons.

The D release DTS uses the same neutral `BOOTBRIDGE-HW-D` label found in the
frozen D candidate. The former sample-specific identity descriptor was also
changed to this release label, with its code and matching host-test updates
recorded as a separate overlay. The reviewed build completed with this neutral label, but its D MBN and
partition differ from the canonical candidate because the sanitized source
build is path-sensitive. This derivative does not inherit the installed
candidate's hardware qualification.

An MBN or partition image built here remains an offline candidate. It does not
prove hardware compatibility, NAND/ECC format, physical inactive-slot binding,
first-install safety, or install readiness. The D installed image and its
separate device evidence remain tied to their exact recorded candidate.

## Source and license

Tracked U-Boot files and their original notices are retained. Only the two
Qualcomm MBN helper scripts carrying confidential/proprietary notices were
excluded. The release packager and build wrapper are new code marked
`GPL-2.0-or-later`; this declaration applies only to those new files. It does
not replace or relicense any upstream component notices.

The private review evidence includes the frozen source commit/tree identity,
per-file SHA-256 manifest, excluded-file hashes, and a per-file identity
sanitization overlay. It is separate from any public release or publication
repository.
