/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NX30_NOR_NAND_H__
#define __NX30_NOR_NAND_H__

#include <linux/types.h>

/* Runtime read formats supported by the HW-D NOR rescue reader. */
enum nx30_nor_nand_ecc {
	NX30_NOR_NAND_ECC4 = 4,
	NX30_NOR_NAND_ECC8 = 8,
};

struct mtd_info;
struct nx30_nor_nand_read_result {
	/* Raw QPIC/MTD read result: 0 or positive bitflips on success, <0 on error. */
	int status;
	size_t data_len;
	/* AUTO_OOB bytes returned by QPIC for the selected ECC layout. */
	size_t oob_len;
	/* Per-call deltas from mtd->ecc_stats. */	u32 failed;
	u32 corrected;
	u32 max_bitflips;
};

/* One-shot exact C8:41, 2KiB/128-byte-spare, 128MiB QPIC init/probe. */
int nx30_nor_nand_init_once(void);
/* Rebuild read-only QPIC read configuration for BCH4 or BCH8. */
int nx30_nor_nand_set_read_ecc(unsigned int bits);
int nx30_nor_nand_restore4(void);
/* HW-D service-only, exact physical extent, ECC4 ordinary-data aperture. */
int nx30_d_qpic_write_window_open(struct mtd_info *mtd, u64 offset,
                                  u64 size, unsigned int ecc_bits);
void nx30_d_qpic_write_window_close(void);
/* Sticky failure restoring the protection byte after array aperture closure. */
int nx30_d_qpic_write_window_close_error(void);
int nx30_d_qpic_write_window_allows(struct mtd_info *mtd, u64 offset,
                                    u64 size);
/* Read one aligned page at a physical NAND offset; no bad-block skipping.
 * Return <0 for API/guard errors. Read/controller result is in result->status.
 */
int nx30_nor_nand_read_page(u64 physical_offset, void *data,
			    size_t data_capacity, void *oob,
			    size_t oob_capacity,
			    struct nx30_nor_nand_read_result *result);

#endif /* __NX30_NOR_NAND_H__ */
