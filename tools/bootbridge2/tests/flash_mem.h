/* SPDX-License-Identifier: GPL-2.0-or-later */
/* In-memory NAND for host tests: erase sets 0xff, programming ANDs bits into
 * an erased page only once, bad blocks and failures can be injected. */
#ifndef BB_FLASH_MEM_H
#define BB_FLASH_MEM_H

#include "../runtime/flash.h"

struct mem_nand {
	uint8_t *data;
	uint8_t *programmed;	/* per page */
	uint8_t *bad;		/* per block */
	uint8_t *unreadable;	/* per block: reads return -EBADMSG */
	int fail_erase_at;	/* fail the Nth erase (1-based), 0 = never */
	int fail_write_at;	/* fail the Nth page write, 0 = never */
	int erases, writes;
	struct bb_flash f;
};

/* Takes a copy of `init` (len == size); NULL init means erased. */
struct mem_nand *mem_nand_new(const char *name, uint32_t offset, uint32_t size,
			      const void *init);
void mem_nand_free(struct mem_nand *m);

#endif
