/* SPDX-License-Identifier: GPL-2.0-or-later */
/* QCA dual-boot BOOTCONFIG record (same layout as u-boot-2016/include/bootconfig.h):
 *   u32 magic_start, u32 age, u32 numaltpart,
 *   16 x { char name[16]; u32 primaryboot; }, u32 magic_end
 * All fields little endian. Only the record at the start of the partition is used. */
#ifndef BB_BOOTCONFIG_H
#define BB_BOOTCONFIG_H

#include <stddef.h>
#include <stdint.h>

#define BB_BC_MAGIC_START	0xA3A2A1A0u
#define BB_BC_MAGIC_END		0xB3B2B1B0u
#define BB_BC_ENTRIES		16
#define BB_BC_NAME_LEN		16
#define BB_BC_RECORD_BYTES	(12 + BB_BC_ENTRIES * 20 + 4)

enum bb_bc_status {
	BB_BC_OK = 0,
	BB_BC_SHORT,		/* buffer smaller than one record */
	BB_BC_BAD_MAGIC,	/* start/end magic wrong (includes try-mode records) */
	BB_BC_BAD_COUNT,	/* numaltpart outside 1..16 */
	BB_BC_BAD_NAME,		/* entry name not printable / not NUL padded */
	BB_BC_NO_ROOTFS,	/* no "rootfs" entry */
	BB_BC_BAD_SELECTOR,	/* rootfs primaryboot not 0 or 1 */
};

struct bb_bc {
	uint32_t age;
	uint32_t count;
	unsigned rootfs_index;
	uint32_t rootfs_selector;
	int have_appsbl;		/* "0:APPSBL" entry present */
	uint32_t appsbl_selector;	/* 0 boots the lower APPSBL copy */
};

enum bb_bc_status bb_bc_parse(const uint8_t *buf, size_t len, struct bb_bc *out);

/* Rewrite the rootfs selector and age of an already parsed record in place.
 * Every other byte of `buf` is left untouched. */
void bb_bc_set(uint8_t *buf, const struct bb_bc *bc, uint32_t selector, uint32_t age);

const char *bb_bc_status_str(enum bb_bc_status s);

#endif
