/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Minimal NAND partition access used by every BootBridge v2 module.
 * The Linux backend (flash_mtd.c) talks to /dev/mtdN; tests use flash_mem.c. */
#ifndef BB_FLASH_H
#define BB_FLASH_H

#include <stddef.h>
#include <stdint.h>

struct bb_flash {
	void *ctx;
	char name[32];
	uint32_t offset;	/* physical offset of the partition on the chip */
	uint32_t size;
	uint32_t erase_size;
	uint32_t page_size;
	/* All return 0 on success, negative errno on failure. */
	int (*read)(void *ctx, uint32_t off, void *buf, size_t len);
	int (*is_bad)(void *ctx, uint32_t block_off);	/* 1 bad, 0 good, <0 error */
	int (*erase)(void *ctx, uint32_t block_off);
	int (*write_page)(void *ctx, uint32_t off, const void *buf);
	void (*close)(void *ctx);
};

int bb_flash_read(const struct bb_flash *f, uint32_t off, void *buf, size_t len);

/* Erase one block and program `data` (erase_size bytes) into it, skipping
 * pages that are entirely 0xff so they stay truly erased, then read the block
 * back and compare. Returns 0, or a negative errno (-EIO for a mismatch). */
int bb_flash_rewrite_block(const struct bb_flash *f, uint32_t block_off,
			   const uint8_t *data);

#endif
