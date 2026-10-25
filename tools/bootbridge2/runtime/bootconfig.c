// SPDX-License-Identifier: GPL-2.0-or-later
#include "bootconfig.h"

#include <string.h>

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put_le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static int name_ok(const uint8_t *n)
{
	size_t len = 0;

	while (len < BB_BC_NAME_LEN && n[len])
		len++;
	if (!len || len == BB_BC_NAME_LEN)
		return 0;
	for (size_t i = 0; i < len; i++)
		if (n[i] < 0x21 || n[i] > 0x7e)
			return 0;
	for (size_t i = len; i < BB_BC_NAME_LEN; i++)
		if (n[i])
			return 0;
	return 1;
}

enum bb_bc_status bb_bc_parse(const uint8_t *buf, size_t len, struct bb_bc *out)
{
	struct bb_bc bc = {0};
	int found = 0;

	if (len < BB_BC_RECORD_BYTES)
		return BB_BC_SHORT;
	if (le32(buf) != BB_BC_MAGIC_START ||
	    le32(buf + BB_BC_RECORD_BYTES - 4) != BB_BC_MAGIC_END)
		return BB_BC_BAD_MAGIC;
	bc.age = le32(buf + 4);
	bc.count = le32(buf + 8);
	if (bc.count < 1 || bc.count > BB_BC_ENTRIES)
		return BB_BC_BAD_COUNT;
	for (unsigned i = 0; i < bc.count; i++) {
		const uint8_t *e = buf + 12 + 20 * i;

		if (!name_ok(e))
			return BB_BC_BAD_NAME;
		if (!strcmp((const char *)e, "rootfs")) {
			if (found)
				return BB_BC_BAD_NAME;
			found = 1;
			bc.rootfs_index = i;
			bc.rootfs_selector = le32(e + BB_BC_NAME_LEN);
		} else if (!strcmp((const char *)e, "0:APPSBL")) {
			bc.have_appsbl = 1;
			bc.appsbl_selector = le32(e + BB_BC_NAME_LEN);
		}
	}
	if (!found)
		return BB_BC_NO_ROOTFS;
	if (bc.rootfs_selector > 1)
		return BB_BC_BAD_SELECTOR;
	*out = bc;
	return BB_BC_OK;
}

void bb_bc_set(uint8_t *buf, const struct bb_bc *bc, uint32_t selector, uint32_t age)
{
	put_le32(buf + 4, age);
	put_le32(buf + 12 + 20 * bc->rootfs_index + BB_BC_NAME_LEN, selector);
}

const char *bb_bc_status_str(enum bb_bc_status s)
{
	switch (s) {
	case BB_BC_OK: return "ok";
	case BB_BC_SHORT: return "short";
	case BB_BC_BAD_MAGIC: return "bad_magic";
	case BB_BC_BAD_COUNT: return "bad_count";
	case BB_BC_BAD_NAME: return "bad_name";
	case BB_BC_NO_ROOTFS: return "no_rootfs";
	case BB_BC_BAD_SELECTOR: return "bad_selector";
	}
	return "unknown";
}
