# Building U-Boot

This page covers `v0.1.0-pre.2`. The repository root `build-release.sh` is the build entry point for the four H3C candidates; the MBN is produced by the independent Python packager in `release-tools/`, so Qualcomm's `elftombn.py` / `mbn_tools.py` are neither needed nor included.

## Toolchain

Candidates were built with **OpenWrt GCC 8.4.0 (`r16847-f8282da11e`) + binutils 2.34**. Rebuilding this source with that toolchain reproduces the published candidates byte for byte.

> [!IMPORTANT]
> Do not build this release with the GCC 5.2.0 / binutils 2.24 combination used for pre.1: the result is roughly 7-8 KB larger and does not match the published digests.

**No verified public download address is provided.** The version string `r16847-f8282da11e` points to an OpenWrt 21.02-series build, but this repository cannot confirm a matching public download and digest, so no link is given. Obtain a GCC 8.4.0 / binutils 2.34 combination from the official OpenWrt SDK (21.02 branch, musl, EABI, `arm_cortex-a7+neon-vfpv4`); a different version string yields different digests.

## Build

Run from the repository root, passing a toolchain prefix, a path mapping, and an output path that does not exist yet:

```sh
CROSS_COMPILE=/opt/toolchains/bin/arm-openwrt-linux- \
KCFLAGS="-ffile-prefix-map=$PWD/=/build/" \
  ./build-release.sh D /tmp/h3c-hw-d-build
```

`KCFLAGS` maps the source root to `/build/`. Published candidates were built this way: binaries carry no build-host path and the result does not depend on where the source lives. Without it your local path is compiled into the binary and the digest differs from the published candidate. The mapping target must be `/build/` and the trailing slash is required.

The toolchain path above is an example; substitute your own prefix. `gcc`, `ld`, `strip` and `nm` must all be available. The output directory must be outside the source tree, and the entry point refuses to reuse an existing path.

The first argument selects the board defconfig: `A` -> `ipq5018_h3c_hw_a_production`, `B` -> `ipq5018_h3c_rt3000_b`, `C` -> `ipq5018_h3c_hw_c_production`, `D` -> `ipq5018_h3c_hw_d_production`.

The NAND profile is selected with environment variables, matching how the 84 candidates were built:

| Variable | Value |
|---|---|
| `H3C_NAND_PROFILE_ID` | profile name starting with `h3c-oem-`, e.g. `h3c-oem-c841-128m-2k-sp128` |
| `H3C_NAND_SOURCE_MASK` | `1`, `2`, `4`, `8` or `16` |
| `H3C_NAND_LOAD_ECC_BITS` | `4` or `8` |
| `H3C_NAND_STORAGE_ECC_BITS` | must equal `H3C_NAND_LOAD_ECC_BITS` |

All four must be supplied together or the build entry point rejects the run.

The build writes ELF, DTB, `appsbl.mbn`, `appsbl.partition.bin`, the config, provenance records and a digest list.

## Version and byte identity

Candidates embed the source revision `b0383f96b3282b0aedbd9765464778a9cf596836`, which is the first 40 hex characters of the source snapshot SHA-256 - not a git commit ID. The snapshot algorithm, sanitised files and build adaptations are documented in [`release-metadata/uboot-v0.1.0-pre.2/SOURCE-PROVENANCE.json`](release-metadata/uboot-v0.1.0-pre.2/SOURCE-PROVENANCE.json).

The six sanitised files take part in the build: sample identity strings were replaced with the neutral label `BOOTBRIDGE-HW-D`. Rebuilding from this repository therefore matches the published candidates, which were verified to contain no original sample string.

A successful build does not establish installation qualification. If your digests differ from the published ones, validate on your own device.

The pre.1 build notes and results remain in [`releases/uboot-v0.1.0-pre.1.md`](releases/uboot-v0.1.0-pre.1.md) and [`release-metadata/uboot-v0.1.0-pre.1/`](release-metadata/uboot-v0.1.0-pre.1/).
