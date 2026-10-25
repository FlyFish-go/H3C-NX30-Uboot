// SPDX-License-Identifier: GPL-2.0-or-later
#include "ubi_check.h"

#include <stdlib.h>
#include <string.h>

#define EC_MAGIC	0x55424923u	/* "UBI#" */
#define VID_MAGIC	0x55424921u	/* "UBI!" */
#define LAYOUT_VOL_ID	0x7fffefffu
#define VTBL_REC_BYTES	172
#define VTBL_MAX	128
#define SQUASHFS_MAGIC	0x73717368u	/* "hsqs" little endian */
#define VOL_STATIC	2

uint32_t bb_ubi_crc32(uint32_t crc, const void *buf, size_t len)
{
	const uint8_t *p = buf;

	while (len--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
	}
	return crc;
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p)
{
	return (uint64_t)be32(p) << 32 | be32(p + 4);
}

static uint16_t be16(const uint8_t *p)
{
	return p[0] << 8 | p[1];
}

struct peb {
	uint32_t vol_id, lnum, data_size, used_ebs, data_crc;
	uint64_t sqnum;
	uint8_t vol_type;
	uint8_t valid;
};

struct scan {
	const struct bb_flash *f;
	struct peb *pebs;
	unsigned count;
	uint32_t vid_off, data_off;
};

/* Newest valid copy of (vol_id, lnum), or -1. */
static int find_leb(const struct scan *s, uint32_t vol_id, uint32_t lnum)
{
	int best = -1;

	for (unsigned i = 0; i < s->count; i++) {
		const struct peb *p = &s->pebs[i];

		if (p->valid && p->vol_id == vol_id && p->lnum == lnum &&
		    (best < 0 || p->sqnum > s->pebs[best].sqnum))
			best = i;
	}
	return best;
}

static int read_leb(const struct scan *s, int peb, uint32_t off, void *buf, size_t len)
{
	return bb_flash_read(s->f, (uint32_t)peb * s->f->erase_size + s->data_off + off,
			     buf, len);
}

/* Volume table: records are taken from LEB 0 of the layout volume, falling
 * back to LEB 1 if LEB 0 is unreadable. Returns the number of records. */
static int load_vtbl(const struct scan *s, uint8_t *vtbl)
{
	for (uint32_t lnum = 0; lnum < 2; lnum++) {
		int peb = find_leb(s, LAYOUT_VOL_ID, lnum);

		if (peb >= 0 && !read_leb(s, peb, 0, vtbl, VTBL_MAX * VTBL_REC_BYTES))
			return VTBL_MAX;
	}
	return 0;
}

static int vtbl_find(const uint8_t *vtbl, const char *name, uint32_t *vol_id)
{
	size_t want = strlen(name);

	for (uint32_t i = 0; i < VTBL_MAX; i++) {
		const uint8_t *r = vtbl + i * VTBL_REC_BYTES;
		uint16_t len = be16(r + 14);

		if (be32(r) == 0 || len != want || memcmp(r + 16, name, len))
			continue;
		if (bb_ubi_crc32(0xffffffffu, r, VTBL_REC_BYTES - 4) != be32(r + 168))
			continue;
		*vol_id = i;
		return 1;
	}
	return 0;
}

static enum bb_ubi_status check_kernel(const struct scan *s, uint32_t vol_id,
				       struct bb_ubi_report *r)
{
	int peb0 = find_leb(s, vol_id, 0);
	uint32_t used, leb_bytes = s->f->erase_size - s->data_off;
	uint8_t *buf;

	if (peb0 < 0)
		return BB_UBI_NO_KERNEL;
	r->kernel_static = s->pebs[peb0].vol_type == VOL_STATIC;
	if (!r->kernel_static) {
		r->kernel_lebs = 1;
		return BB_UBI_OK;
	}
	used = s->pebs[peb0].used_ebs;
	if (!used || used > s->count)
		return BB_UBI_NO_KERNEL;
	buf = malloc(leb_bytes);
	if (!buf)
		return BB_UBI_NO_MEM;
	for (uint32_t lnum = 0; lnum < used; lnum++) {
		int peb = find_leb(s, vol_id, lnum);
		uint32_t size;

		if (peb < 0) {
			free(buf);
			return BB_UBI_NO_KERNEL;
		}
		size = s->pebs[peb].data_size;
		if (size > leb_bytes || read_leb(s, peb, 0, buf, size)) {
			free(buf);
			return BB_UBI_NO_KERNEL;
		}
		if (bb_ubi_crc32(0xffffffffu, buf, size) != s->pebs[peb].data_crc) {
			free(buf);
			return BB_UBI_KERNEL_CRC;
		}
		r->kernel_lebs++;
		r->kernel_bytes += size;
	}
	free(buf);
	return BB_UBI_OK;
}

enum bb_ubi_status bb_ubi_check(const struct bb_flash *f, struct bb_ubi_report *r)
{
	struct scan s = { .f = f };
	enum bb_ubi_status ret;
	uint8_t hdr[64], *vtbl = NULL;
	uint32_t kernel_id, rootfs_id, magic;
	int peb;

	memset(r, 0, sizeof(*r));
	s.count = r->pebs = f->size / f->erase_size;
	s.pebs = calloc(s.count, sizeof(*s.pebs));
	if (!s.pebs)
		return BB_UBI_NO_MEM;

	for (unsigned i = 0; i < s.count; i++) {
		uint32_t base = i * f->erase_size, vid, data;
		struct peb *p = &s.pebs[i];

		if (f->is_bad(f->ctx, base)) {
			r->bad_pebs++;
			continue;
		}
		if (bb_flash_read(f, base, hdr, sizeof(hdr))) {
			r->unreadable++;
			continue;
		}
		if (be32(hdr) != EC_MAGIC ||
		    bb_ubi_crc32(0xffffffffu, hdr, 60) != be32(hdr + 60))
			continue;
		r->ec_valid++;
		vid = be32(hdr + 16);
		data = be32(hdr + 20);
		if (vid < 64 || vid + 64 > f->erase_size || data <= vid || data >= f->erase_size)
			continue;
		if (!s.vid_off) {
			s.vid_off = vid;
			s.data_off = data;
		} else if (vid != s.vid_off || data != s.data_off) {
			continue;
		}
		if (bb_flash_read(f, base + vid, hdr, sizeof(hdr))) {
			r->unreadable++;
			continue;
		}
		if (be32(hdr) != VID_MAGIC ||
		    bb_ubi_crc32(0xffffffffu, hdr, 60) != be32(hdr + 60))
			continue;	/* erased/free PEB */
		p->vol_type = hdr[5];
		p->vol_id = be32(hdr + 8);
		p->lnum = be32(hdr + 12);
		p->data_size = be32(hdr + 20);
		p->used_ebs = be32(hdr + 24);
		p->data_crc = be32(hdr + 32);
		p->sqnum = be64(hdr + 40);
		p->valid = 1;
	}

	ret = BB_UBI_NOT_UBI;
	if (!r->ec_valid || !s.data_off)
		goto out;
	ret = BB_UBI_NO_MEM;
	vtbl = malloc(VTBL_MAX * VTBL_REC_BYTES);
	if (!vtbl)
		goto out;
	ret = BB_UBI_NO_VTBL;
	if (f->erase_size - s.data_off < VTBL_MAX * VTBL_REC_BYTES || !load_vtbl(&s, vtbl))
		goto out;
	for (uint32_t i = 0; i < VTBL_MAX; i++)
		if (be32(vtbl + i * VTBL_REC_BYTES))
			r->volumes++;

	ret = BB_UBI_NO_KERNEL;
	if (!vtbl_find(vtbl, "kernel", &kernel_id))
		goto out;
	ret = check_kernel(&s, kernel_id, r);
	if (ret)
		goto out;

	ret = BB_UBI_NO_ROOTFS;
	if (!vtbl_find(vtbl, "ubi_rootfs", &rootfs_id))
		goto out;
	peb = find_leb(&s, rootfs_id, 0);
	if (peb < 0 || read_leb(&s, peb, 0, hdr, 4))
		goto out;
	magic = hdr[0] | hdr[1] << 8 | hdr[2] << 16 | (uint32_t)hdr[3] << 24;
	ret = magic == SQUASHFS_MAGIC ? BB_UBI_OK : BB_UBI_ROOTFS_MAGIC;
out:
	free(vtbl);
	free(s.pebs);
	return ret;
}

const char *bb_ubi_status_str(enum bb_ubi_status s)
{
	switch (s) {
	case BB_UBI_OK: return "ok";
	case BB_UBI_NO_MEM: return "no_memory";
	case BB_UBI_NOT_UBI: return "not_ubi";
	case BB_UBI_NO_VTBL: return "no_volume_table";
	case BB_UBI_NO_KERNEL: return "no_kernel";
	case BB_UBI_KERNEL_CRC: return "kernel_crc";
	case BB_UBI_NO_ROOTFS: return "no_rootfs";
	case BB_UBI_ROOTFS_MAGIC: return "rootfs_not_squashfs";
	}
	return "unknown";
}
