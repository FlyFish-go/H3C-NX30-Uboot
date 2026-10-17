# HW-A OEM dashboard UBIFS read audit — 2026-10-09

## Evidence correction

An earlier offline report used `ubireader`'s `walk.index()` data-node convenience offsets 48 and 4192 as physical data-node offsets. That interpretation is withdrawn. In this version of the library those values are derived from index-node location and are not the physical branch targets.

Direct traversal of the current raw UBIFS leaf branch table resolves inode 65, data block 0 to LEB 13 offset 0, length 4144, and data block 1 to LEB 13 offset 4144, length 4144. Reading node headers directly from those offsets validates the UBIFS DATA node type, length, inode/key, payload length, and node CRC. This raw inspection does not replay the UBIFS journal and therefore does not establish the post-mount TNC state.

The current high snapshot is `current-high-dashboard-20261009.bin`, SHA-256 `86422acec5c45998a98aa9d62a022bfe42affcfeadf1e31fc0d5e8bbbeddbdc2`. A whitelist-only UBIFS walk found `rootfs_data` volume ID 4 with 8 mapped blocks and one `sysupgrade.tgz` archive of 28,776 bytes, SHA-256 `90e6787cb9f777c57ef2159597710e2962156ea874269b28afdeac49a1ea06b7`; the parser reported zero indexed bad LEBs. No archive or configuration payload was emitted.

## Runtime failure and source change

The bounded RAM run for the high slot returned a malformed node header at LEB 13 offset 4144 (observed type 135 and length 3435273907; expected a 4144-byte DATA node), while offset 0 succeeded. Source inspection pins the mismatch in the QPIC path. For a nonzero starting column, `qpic_nand_read_datbuf()` selects the one-page `pad_dat`; `qpic_nand_multi_page_read()` sets `multi_page_copy`; then `qpic_nand_read_datcopy()` takes the multi-page branch that advances the reported length without copying the requested slice from `pad_dat` to the caller. The caller therefore does not receive the raw bytes at the node offset. The path also risks writing multiple pages through the one-page pad buffer.

`drivers/mtd/ubi/io.c` routes reads with an unaligned start, length, or DMA destination through an `ARCH_DMA_MINALIGN`-aligned buffer and page-rounded, page-aligned read. That forces QPIC onto the direct destination-buffer branch. It copies only the original requested span after a complete underlying read, preserves MTD ECC/error returns, and rejects short page-rounded reads without copying or claiming a complete range. UBIFS node CRC and TNC validation remain enabled.

An additional `override_pending` cause was a malformed allowlist scalar: the source archive SHA literal had 63 hexadecimal characters and could never match a SHA-256. The independently computed current archive hash above matches the previously audited archive exactly after restoring the missing final `7`. The production constant is corrected. The host test checks that the extracted production constant is exactly 64 lowercase hex characters, compiles it into a fixture with a 65-byte C array assertion, and rejects short and nonhex strings.

The production-helper C test `test/ubi_mtd_aligned_hosttest.py` covers offsets 0 and 4144, a three-page read, aligned DMA destinations, preserved `-EUCLEAN`, and short reads without copy/full retlen. `test/slots_oem_overlay_hosttest.py` covers the archive hash shape and mapped overlay resolver. These host checks pass. High-slot runtime resolution remains pending a rebuild and bounded RAM retest of the corrected source.

## Result limits

The offline raw image and archive checks establish their exact input hashes and whitelist fields, not a completed runtime dashboard result. This work does not read or display configuration secrets or alter flash. Unsupported map or overlay states remain unresolved.

<!-- HW_A_ROOT_FINAL9_ACTUAL_BEGIN -->
## Root 最终实机核验（2026-10-09）

Final9 18:36:35 已在 RC-A2-512 的 RAM 与 NAND 零串口输入冷启动中核验，两槽后台字段均 resolved：低槽 RC3000V100R006、高槽 RC3000V100R012。非空高槽允许归档 SHA-256 已独立重构确认完整64位为 90e6787cb9f777c57ef2159597710e2962156ea874269b28afdeac49a1ea06b7；旧常量漏末尾7的问题已修复且严格hex测试通过。UBI对齐读取及UBIFS句柄修复已实际消除读取节点错误。只读/恢复阶段完整128MiB不变；后续普通OEM启动数据尾变化单独记录，不扩大该只读保护结论。该字段依据原厂getter和当前文件推导，不等同OEM Web登录查询。最终报告位于主WSL仓库 docs/H3C-HW-A-UBOOT-FUNCTIONS-ROUND2-20261009.md。其他地区覆盖配置保持保守判定。本段是构建冻结后的文档记录，不改动该构建的C源码或历史freeze清单。
<!-- HW_A_ROOT_FINAL9_ACTUAL_END -->
