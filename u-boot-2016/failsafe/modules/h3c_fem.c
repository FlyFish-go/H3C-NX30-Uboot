// SPDX-License-Identifier: GPL-2.0+
/* MP02.1 5 GHz FEM check, shown on the rescue pages so a D-PCB without FEMs
 * (RC3000 B0) is pointed at the HW-B U-Boot and firmware. Read-only: ART is
 * only read, once, when the web UI first asks; the boot path never calls it.
 */
#ifdef H3C_FEM_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
#include "../../include/h3c_fem.h"
#else
#include <common.h>
#include <nand.h>
#include <asm/arch-qca-common/smem.h>
#include <h3c_build_id.h>
#include <h3c_fem.h>
#endif

enum h3c_fem_state h3c_fem_classify(const u8 *blk, size_t len)
{
	if (!blk || len < H3C_FEM_BYTES)
		return H3C_FEM_UNKNOWN;
	if (blk[0] != 0x01 || blk[1] != 0x00 || blk[2] != 0x04 || blk[3] != 0x04)
		return H3C_FEM_UNKNOWN;
	if (blk[0x3a] == 0x60 && blk[0x45] == 0x09)
		return H3C_FEM_PRESENT;
	if (blk[0x3a] == 0x50 && blk[0x45] == 0x01)
		return H3C_FEM_ABSENT;
	return H3C_FEM_UNKNOWN;
}

const char *h3c_fem_group(enum h3c_fem_state state)
{
	switch (state) {
	case H3C_FEM_PRESENT: return "D";
	case H3C_FEM_ABSENT: return "B";
	default: return NULL;
	}
}

const char *h3c_fem_name(enum h3c_fem_state state)
{
	switch (state) {
	case H3C_FEM_PRESENT: return "present";
	case H3C_FEM_ABSENT: return "absent";
	case H3C_FEM_UNKNOWN: return "unknown";
	default: return "n/a";
	}
}

#ifndef H3C_FEM_HOST_TEST
enum h3c_fem_state h3c_fem_probe(void)
{
	static int done;
	static enum h3c_fem_state cached;
	qca_smem_flash_info_t *sfi = &qca_smem_flash_info;
	const char *board = h3c_build_board();
	uint32_t start = 0, blocks = 0;
	u8 buf[H3C_FEM_BYTES];
	size_t len = sizeof(buf);
	loff_t off;

	if (done)
		return cached;
	done = 1;
	cached = H3C_FEM_NA;
	if (!board || (board[0] != 'B' && board[0] != 'D') || board[1])
		return cached;
	cached = H3C_FEM_UNKNOWN;
	if (sfi->flash_type != SMEM_BOOT_NAND_FLASH &&
	    sfi->flash_type != SMEM_BOOT_QSPI_NAND_FLASH)
		return cached;
	if (smem_getpart((char *)"0:ART", &start, &blocks) < 0 || !blocks ||
	    (loff_t)sfi->flash_block_size * blocks < H3C_FEM_ART_OFFSET + H3C_FEM_BYTES)
		return cached;
	off = (loff_t)sfi->flash_block_size * start + H3C_FEM_ART_OFFSET;
	if (nand_read(&nand_info[CONFIG_NAND_FLASH_INFO_IDX], off, &len, buf) < 0 ||
	    len != sizeof(buf))
		return cached;
	cached = h3c_fem_classify(buf, len);
	return cached;
}
#endif
