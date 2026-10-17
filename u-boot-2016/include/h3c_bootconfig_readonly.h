/* SPDX-License-Identifier: GPL-2.0 */
#ifndef H3C_BOOTCONFIG_READONLY_H
#define H3C_BOOTCONFIG_READONLY_H

/* OEM little-endian wire record. It contains no stored checksum. */
#define H3C_BC_RO_BYTES 336U
#define H3C_BC_RO_MAX_PARTS 16U
#define H3C_BC_RO_NAME_BYTES 16U
#define H3C_BC_RO_OK 0
#define H3C_BC_RO_INVALID (-1)
#define H3C_BC_RO_CHANGED (-2)
#define H3C_BC_RO_CONFLICT (-3)
#define H3C_BC_RO_IO (-4)

/* A nonzero read status is failure; short reads are failure even with status 0.
 * No writer, repair operation, environment, or age preference is accepted. */
typedef int (*h3c_bc_ro_read_fn)(void *context, unsigned int copy,
	unsigned char *record, unsigned int requested, unsigned int *actual);
struct h3c_bc_ro_result {
	unsigned int selector;
	unsigned int selected_copy;
	unsigned int age;
	unsigned int crc32[2]; /* Calculated diagnostics, never an on-wire CRC. */
	unsigned int valid_mask;
};

static inline unsigned int h3c_bc_ro_u32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
		((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static inline int h3c_bc_ro_equal(const unsigned char *a,
	const unsigned char *b, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; ++i)
		if (a[i] != b[i])
			return 0;
	return 1;
}
static inline unsigned int h3c_bc_ro_crc32(const unsigned char *p)
{
	unsigned int i, j, c = 0xffffffffU;
	for (i = 0; i < H3C_BC_RO_BYTES; ++i) {
		c ^= p[i];
		for (j = 0; j < 8; ++j)
			c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U)));
	}
	return c ^ 0xffffffffU;
}
static inline int h3c_bc_ro_name_len(const unsigned char *name)
{
	unsigned int i;
	for (i = 0; i < H3C_BC_RO_NAME_BYTES; ++i) {
		if (!name[i])
			return i ? (int)i : -1;
		if (name[i] < 0x21 || name[i] > 0x7e)
			return -1;
	}
	return -1;
}
/* Return 1 for a valid record, 0 for an unusable envelope, -1 for ambiguous
 * semantics. An ambiguous record cannot be hidden by a good other copy. */
static inline int h3c_bc_ro_validate(const unsigned char *p,
	const unsigned char *wanted, unsigned int wanted_len, unsigned int *selector)
{
	unsigned int n, i, j, found = 0;
	int len, prev_len;
	if (h3c_bc_ro_u32(p) == 0xa3a2a1a1U)
		return -1; /* Unknown trymode cannot be hidden by another copy. */
	if (h3c_bc_ro_u32(p) != 0xa3a2a1a0U ||
		h3c_bc_ro_u32(p + 332) != 0xb3b2b1b0U)
		return 0;
	n = h3c_bc_ro_u32(p + 8);
	if (!n || n > H3C_BC_RO_MAX_PARTS)
		return -1;
	for (i = 0; i < n; ++i) {
		const unsigned char *e = p + 12 + i * 20;
		len = h3c_bc_ro_name_len(e);
		if (len < 0 || h3c_bc_ro_u32(e + 16) > 1)
			return -1;
		for (j = 0; j < i; ++j) {
			const unsigned char *prev = p + 12 + j * 20;
			prev_len = h3c_bc_ro_name_len(prev);
			if (prev_len == len && h3c_bc_ro_equal(e, prev,
				(unsigned int)len))
				return -1;
		}
		if ((unsigned int)len == wanted_len &&
			h3c_bc_ro_equal(e, wanted, wanted_len)) {
			*selector = h3c_bc_ro_u32(e + 16);
			found = 1;
		}
	}
	return found ? 1 : -1;
}
static inline int h3c_bc_ro_select(h3c_bc_ro_read_fn read_fn, void *context,
	const char *part_name, struct h3c_bc_ro_result *result)
{
	unsigned char data[2][2][H3C_BC_RO_BYTES];
	unsigned int actual[2][2], selector[2] = {0, 0}, length = 0;
	int status[2][2], valid[2];
	unsigned int copy, pass, chosen;
	struct h3c_bc_ro_result output = {0, 0, 0, {0, 0}, 0};
	if (!read_fn || !part_name || !result || sizeof(unsigned int) != 4)
		return H3C_BC_RO_INVALID;
	while (length < H3C_BC_RO_NAME_BYTES && part_name[length]) {
		if ((unsigned char)part_name[length] < 0x21 ||
			(unsigned char)part_name[length] > 0x7e)
			return H3C_BC_RO_INVALID;
		++length;
	}
	if (!length || length == H3C_BC_RO_NAME_BYTES)
		return H3C_BC_RO_INVALID;
	/* Read both snapshots in separate passes; never select after one pass. */
	for (pass = 0; pass < 2; ++pass)
		for (copy = 0; copy < 2; ++copy) {
			actual[copy][pass] = 0;
			status[copy][pass] = read_fn(context, copy, data[copy][pass],
				H3C_BC_RO_BYTES, &actual[copy][pass]);
		}
	for (copy = 0; copy < 2; ++copy) {
		if (status[copy][0] != status[copy][1] ||
			actual[copy][0] != actual[copy][1])
			return H3C_BC_RO_CHANGED;
		if (status[copy][0] || actual[copy][0] != H3C_BC_RO_BYTES) {
			valid[copy] = 0;
			continue;
		}
		if (!h3c_bc_ro_equal(data[copy][0], data[copy][1], H3C_BC_RO_BYTES))
			return H3C_BC_RO_CHANGED;
		output.crc32[copy] = h3c_bc_ro_crc32(data[copy][0]);
		valid[copy] = h3c_bc_ro_validate(data[copy][0],
			(const unsigned char *)part_name, length, &selector[copy]);
		if (valid[copy] < 0)
			return H3C_BC_RO_INVALID;
		if (valid[copy])
			output.valid_mask |= 1U << copy;
	}
	if (!output.valid_mask)
		return H3C_BC_RO_IO;
	if (output.valid_mask == 3 &&
		!h3c_bc_ro_equal(data[0][0], data[1][0], H3C_BC_RO_BYTES))
		return H3C_BC_RO_CONFLICT;
	chosen = valid[0] ? 0 : 1;
	output.selected_copy = chosen;
	output.selector = selector[chosen];
	output.age = h3c_bc_ro_u32(data[chosen][0] + 4);
	*result = output; /* Caller cannot consume partial output on failure. */
	return H3C_BC_RO_OK;
}
#endif
