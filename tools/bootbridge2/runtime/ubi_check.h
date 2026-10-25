/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Read-only sanity check of a raw UBI partition: is there a bootable OEM
 * system in it? Parses EC/VID headers and the volume table directly from
 * flash, never attaches UBI and never writes. */
#ifndef BB_UBI_CHECK_H
#define BB_UBI_CHECK_H

#include "flash.h"

enum bb_ubi_status {
	BB_UBI_OK = 0,
	BB_UBI_NO_MEM,
	BB_UBI_NOT_UBI,		/* no valid EC header at all */
	BB_UBI_NO_VTBL,		/* no readable volume table */
	BB_UBI_NO_KERNEL,	/* volume "kernel" missing or incomplete */
	BB_UBI_KERNEL_CRC,	/* static kernel volume data CRC mismatch */
	BB_UBI_NO_ROOTFS,	/* volume "ubi_rootfs" missing or LEB 0 unreadable */
	BB_UBI_ROOTFS_MAGIC,	/* ubi_rootfs LEB 0 does not start with squashfs */
};

struct bb_ubi_report {
	unsigned pebs;		/* blocks in the partition */
	unsigned bad_pebs;
	unsigned ec_valid;
	unsigned unreadable;	/* read errors (uncorrectable ECC etc.) */
	unsigned volumes;
	unsigned kernel_lebs;
	uint32_t kernel_bytes;
	int kernel_static;
};

enum bb_ubi_status bb_ubi_check(const struct bb_flash *f, struct bb_ubi_report *r);
const char *bb_ubi_status_str(enum bb_ubi_status s);

/* UBI CRC32: crc32_le seeded with 0xffffffff, no final inversion. */
uint32_t bb_ubi_crc32(uint32_t crc, const void *buf, size_t len);

#endif
