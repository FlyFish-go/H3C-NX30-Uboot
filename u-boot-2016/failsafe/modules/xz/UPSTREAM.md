XZ Embedded upstream: https://github.com/tukaani-project/xz-embedded
Commit: ae63ae3a36ed01724674e8f3d750dc47bf125410
Vendored files retain upstream 0BSD SPDX headers; see COPYING.

Local integration: `xz_private.h` selects the standalone `xz_config.h` adapter
when `XZ_UBOOT_BUILD` is defined. This avoids Linux-kernel-only headers while
retaining the single-call decoder, ARM/ARM-Thumb BCJ filters, U-Boot allocator,
and the vendored internal CRC32 implementation.
