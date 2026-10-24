/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __RT3000_MIBIB_H__
#define __RT3000_MIBIB_H__
#ifdef RT3_MIBIB_HOST_TEST
#include <stdint.h>
typedef uint8_t rt3_mibib_u8;
typedef uint32_t rt3_mibib_u32;
#else
#include <linux/types.h>
typedef u8 rt3_mibib_u8;
typedef u32 rt3_mibib_u32;
#endif

#define RT3_MIBIB_OFFSET 0x00080000U
#define RT3_MIBIB_SIZE 0x00080000U
#define RT3_MIBIB_BLOCK 0x00020000U
#define RT3_MIBIB_PAGE 0x00000800U
#define RT3_MIBIB_COPIES 2U
/* Offset of a copy's SMEM-format partition table page. */
#define RT3_MIBIB_TABLE_PAGE(copy) ((copy) * RT3_MIBIB_BLOCK + RT3_MIBIB_PAGE)

/* Validate a whole 512 KiB MIBIB partition image: two copies (blocks 0 and 1)
 * with header, SMEM partition table, user table and CRC trailer each, equal
 * tables in both copies, distinct ages, and empty blocks 2 and 3. Board layout
 * rules are checked separately against the SMEM table page. ages may be NULL.
 */
int rt3_mibib_check(const rt3_mibib_u8 *image, rt3_mibib_u32 size,
		    rt3_mibib_u32 ages[RT3_MIBIB_COPIES]);

/* Age stored in one erase block's MIBIB header, or -1 when it is not a copy. */
int rt3_mibib_block_age(const rt3_mibib_u8 *block, rt3_mibib_u32 size,
			rt3_mibib_u32 *age);
#endif
