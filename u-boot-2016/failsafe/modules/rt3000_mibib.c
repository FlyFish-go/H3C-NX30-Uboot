// SPDX-License-Identifier: GPL-2.0+
/* MIBIB (SBL partition table) image format. Each copy is one erase block:
 *   page 0  header  FE569FAC CD7F127A, version 4, age
 *   page 1  SMEM partition table 55EE73AA E35EBDDB, version 4, count,
 *           32 slots of {name[16], start, size, attr} blocks; unused slots zero
 *   page 2  user table AA7D1B9A 1F7D48BC, version 4, count, same names
 *   page 3  trailer 9D41BEA1 F1DED2EA, version 1, CRC-32 (MSB first, poly
 *           04C11DB7, init 0) over pages 0..2
 * Everything else in the block, and blocks 2 and 3, is 0xff.
 */
#include <rt3000_mibib.h>
#ifdef RT3_MIBIB_HOST_TEST
#include <string.h>
#else
#include <common.h>
#endif

#define HDR_MAGIC1 0xfe569facU
#define HDR_MAGIC2 0xcd7f127aU
#define SYS_MAGIC1 0x55ee73aaU
#define SYS_MAGIC2 0xe35ebddbU
#define USR_MAGIC1 0xaa7d1b9aU
#define USR_MAGIC2 0x1f7d48bcU
#define END_MAGIC1 0x9d41bea1U
#define END_MAGIC2 0xf1ded2eaU
#define TABLE_VERSION 4U
#define END_VERSION 1U
#define HDR_BYTES 16U
#define ENTRY_BYTES 28U
#define TABLE_SLOTS 32U
#define TABLE_BYTES (HDR_BYTES + TABLE_SLOTS * ENTRY_BYTES)
#define CRC_SPAN (3U * RT3_MIBIB_PAGE)

static rt3_mibib_u32 le32(const rt3_mibib_u8 *p)
{
	return (rt3_mibib_u32)p[0] | ((rt3_mibib_u32)p[1] << 8) |
	       ((rt3_mibib_u32)p[2] << 16) | ((rt3_mibib_u32)p[3] << 24);
}

static int all_bytes(const rt3_mibib_u8 *p, rt3_mibib_u32 n, rt3_mibib_u8 v)
{
	rt3_mibib_u32 i;

	for (i = 0; i < n; i++)
		if (p[i] != v)
			return 0;
	return 1;
}

static rt3_mibib_u32 crc32_msb(const rt3_mibib_u8 *p, rt3_mibib_u32 n)
{
	rt3_mibib_u32 crc = 0, i;
	int bit;

	for (i = 0; i < n; i++) {
		crc ^= (rt3_mibib_u32)p[i] << 24;
		for (bit = 0; bit < 8; bit++)
			crc = crc & 0x80000000U ? (crc << 1) ^ 0x04c11db7U : crc << 1;
	}
	return crc;
}

/* A table page: magics, version, 1..32 named entries, zeroed spare slots and
 * 0xff padding. Returns the entry count or 0. */
static rt3_mibib_u32 table_page(const rt3_mibib_u8 *p, rt3_mibib_u32 magic1,
				rt3_mibib_u32 magic2)
{
	rt3_mibib_u32 n, i;

	if (le32(p) != magic1 || le32(p + 4) != magic2 ||
	    le32(p + 8) != TABLE_VERSION)
		return 0;
	n = le32(p + 12);
	if (!n || n > TABLE_SLOTS)
		return 0;
	for (i = 0; i < n; i++) {
		const rt3_mibib_u8 *name = p + HDR_BYTES + i * ENTRY_BYTES;

		if (!name[0] || !memchr(name, 0, 16))
			return 0;
	}
	if (!all_bytes(p + HDR_BYTES + n * ENTRY_BYTES,
		       (TABLE_SLOTS - n) * ENTRY_BYTES, 0) ||
	    !all_bytes(p + TABLE_BYTES, RT3_MIBIB_PAGE - TABLE_BYTES, 0xff))
		return 0;
	return n;
}

int rt3_mibib_block_age(const rt3_mibib_u8 *b, rt3_mibib_u32 size,
			rt3_mibib_u32 *age)
{
	rt3_mibib_u32 n, i;

	if (!b || size < RT3_MIBIB_BLOCK || le32(b) != HDR_MAGIC1 ||
	    le32(b + 4) != HDR_MAGIC2 || le32(b + 8) != TABLE_VERSION ||
	    !all_bytes(b + HDR_BYTES, RT3_MIBIB_PAGE - HDR_BYTES, 0xff))
		return -1;
	n = table_page(b + RT3_MIBIB_PAGE, SYS_MAGIC1, SYS_MAGIC2);
	if (!n || table_page(b + 2 * RT3_MIBIB_PAGE, USR_MAGIC1, USR_MAGIC2) != n)
		return -1;
	for (i = 0; i < n; i++)
		if (memcmp(b + RT3_MIBIB_PAGE + HDR_BYTES + i * ENTRY_BYTES,
			   b + 2 * RT3_MIBIB_PAGE + HDR_BYTES + i * ENTRY_BYTES, 16))
			return -1;
	if (le32(b + CRC_SPAN) != END_MAGIC1 || le32(b + CRC_SPAN + 4) != END_MAGIC2 ||
	    le32(b + CRC_SPAN + 8) != END_VERSION ||
	    le32(b + CRC_SPAN + 12) != crc32_msb(b, CRC_SPAN) ||
	    !all_bytes(b + CRC_SPAN + HDR_BYTES, RT3_MIBIB_PAGE - HDR_BYTES, 0xff) ||
	    !all_bytes(b + 4 * RT3_MIBIB_PAGE, RT3_MIBIB_BLOCK - 4 * RT3_MIBIB_PAGE, 0xff))
		return -1;
	if (age)
		*age = le32(b + 12);
	return 0;
}

int rt3_mibib_check(const rt3_mibib_u8 *image, rt3_mibib_u32 size,
		    rt3_mibib_u32 ages[RT3_MIBIB_COPIES])
{
	rt3_mibib_u32 age[RT3_MIBIB_COPIES], c;

	if (!image || size != RT3_MIBIB_SIZE)
		return -1;
	for (c = 0; c < RT3_MIBIB_COPIES; c++)
		if (rt3_mibib_block_age(image + c * RT3_MIBIB_BLOCK,
					RT3_MIBIB_BLOCK, &age[c]))
			return -1;
	/* Both copies describe the same layout; only the age differs. */
	if (age[0] == age[1] ||
	    memcmp(image + RT3_MIBIB_PAGE, image + RT3_MIBIB_BLOCK + RT3_MIBIB_PAGE,
		   2 * RT3_MIBIB_PAGE) ||
	    !all_bytes(image + RT3_MIBIB_COPIES * RT3_MIBIB_BLOCK,
		       RT3_MIBIB_SIZE - RT3_MIBIB_COPIES * RT3_MIBIB_BLOCK, 0xff))
		return -1;
	if (ages)
		for (c = 0; c < RT3_MIBIB_COPIES; c++)
			ages[c] = age[c];
	return 0;
}
