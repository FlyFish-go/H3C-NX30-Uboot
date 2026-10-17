/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_B_FW_WRITE_H__
#define __RT3000_B_FW_WRITE_H__
#include <linux/types.h>
struct mtd_info;
/* Open a synchronous, exact-MTD/exact-byte-range ECC-only QPIC write window. */
int rt3_fw_write_window_open(struct mtd_info *mtd, u64 offset, u64 size);
/* B-only ECC window for a full, uniquely named partition in the validated named-NAND allowlist. */
int rt3_named_nand_write_window_open(struct mtd_info *mtd, const char *name,
				     u64 offset, u64 size);
/* Shared exact-name aperture for vetted primary metadata writers; accepted ranges are board/layout checked. */
int rt3_metadata_write_window_open(struct mtd_info *mtd, const char *name,
				   u64 offset, u64 size);
/* HW-C production-only BOOTCONFIG aperture; RAM readonly profiles reject it. */
int rt3_bootconfig_write_window_open(struct mtd_info *mtd, const char *name,
				     u64 offset, u64 size);
void rt3_fw_write_window_close(void);
int rt3_fw_write_window_close_status(void);
int rt3_fw_write_window_allows(struct mtd_info *mtd, u64 offset, u64 size);
#endif
