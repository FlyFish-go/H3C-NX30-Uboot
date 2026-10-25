// SPDX-License-Identifier: GPL-2.0-or-later
#include "backup.h"
#include "sha256.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The ZIP is hashed while it is written so the page can show its SHA-256. */
struct zout {
	FILE *f;
	struct bb_sha256 sha;
	uint64_t off;
	int err;
};

static void zput(struct zout *z, const void *p, size_t n)
{
	if (z->err)
		return;
	if (fwrite(p, 1, n, z->f) != n) {
		z->err = -EIO;
		return;
	}
	bb_sha256_update(&z->sha, p, n);
	z->off += n;
}

static void le16(uint8_t *p, uint16_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}

static void le32(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
	p[2] = v >> 16;
	p[3] = v >> 24;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *p, size_t n)
{
	crc = ~crc;
	while (n--) {
		crc ^= *p++;
		for (int k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & -(crc & 1));
	}
	return ~crc;
}

/* Local header with sizes/CRC known up front (data is in RAM per entry). */
static void local_header(struct zout *z, const char *name, uint32_t crc, uint32_t size)
{
	uint8_t h[30];
	size_t nl = strlen(name);

	memset(h, 0, sizeof(h));
	le32(h, 0x04034b50);
	le16(h + 4, 20);
	le32(h + 14, crc);
	le32(h + 18, size);
	le32(h + 22, size);
	le16(h + 26, nl);
	zput(z, h, sizeof(h));
	zput(z, name, nl);
}

struct central {
	char name[48];
	uint32_t crc, size, offset;
};

static void central_entry(struct zout *z, const struct central *c)
{
	uint8_t h[46];
	size_t nl = strlen(c->name);

	memset(h, 0, sizeof(h));
	le32(h, 0x02014b50);
	le16(h + 4, 20);
	le16(h + 6, 20);
	le32(h + 16, c->crc);
	le32(h + 20, c->size);
	le32(h + 24, c->size);
	le16(h + 28, nl);
	le32(h + 42, c->offset);
	zput(z, h, sizeof(h));
	zput(z, c->name, nl);
}

static void add_file(struct zout *z, struct central *c, const char *name, const uint8_t *data,
		     uint32_t size)
{
	snprintf(c->name, sizeof(c->name), "%s", name);
	c->crc = crc32_update(0, data, size);
	c->size = size;
	c->offset = z->off;
	local_header(z, name, c->crc, size);
	zput(z, data, size);
}

/* Read a whole partition; bad or unreadable blocks become 0xff. */
static uint8_t *read_partition(const struct bb_flash *f, unsigned *bad, unsigned *unreadable,
			       char *notes, size_t notes_len)
{
	uint8_t *buf = malloc(f->size);
	size_t used = strlen(notes);

	if (!buf)
		return NULL;
	for (uint32_t off = 0; off < f->size; off += f->erase_size) {
		const char *why = NULL;

		if (f->is_bad(f->ctx, off) > 0) {
			why = "bad";
			(*bad)++;
		} else if (bb_flash_read(f, off, buf + off, f->erase_size)) {
			why = "unreadable";
			(*unreadable)++;
		}
		if (why) {
			memset(buf + off, 0xff, f->erase_size);
			if (used + 80 < notes_len)
				used += snprintf(notes + used, notes_len - used,
						 "%s{\"offset\":%u,\"block\":\"%s\"}", used ? "," : "",
						 f->offset + off, why);
		}
	}
	return buf;
}

int bb_backup_write(FILE *out, const struct bb_backup_entry *e, unsigned n,
		    struct bb_backup_result *r)
{
	struct central cd[BB_BACKUP_MAX + 1];
	struct zout z = { .f = out };
	char *manifest;
	size_t mlen = 0, mcap = 8192 + n * 512;
	uint8_t eocd[22], d[32];
	uint64_t cd_start;

	memset(r, 0, sizeof(*r));
	if (n > BB_BACKUP_MAX)
		return -EINVAL;
	manifest = malloc(mcap);
	if (!manifest)
		return -ENOMEM;
	bb_sha256_init(&z.sha);
	mlen = snprintf(manifest, mcap,
			"{\"schema\":\"bb-backup-v1\",\"data_format\":\"ecc-decoded-main-area\","
			"\"contains_oob\":false,\"excludes\":\"rootfs slots\",\"entries\":[");
	for (unsigned i = 0; i < n && !z.err; i++) {
		const struct bb_flash *f = e[i].part;
		char notes[1024] = "";
		struct bb_sha256 s;
		char hex[65];
		uint8_t *data = read_partition(f, &r->bad_blocks, &r->unreadable_blocks, notes,
					       sizeof(notes));

		if (!data) {
			free(manifest);
			return -ENOMEM;
		}
		bb_sha256_init(&s);
		bb_sha256_update(&s, data, f->size);
		bb_sha256_final(&s, d);
		bb_sha256_hex(d, hex);
		add_file(&z, &cd[i], e[i].file, data, f->size);
		free(data);
		mlen += snprintf(manifest + mlen, mcap - mlen,
				 "%s{\"name\":\"%s\",\"partition\":\"%s\",\"role\":\"%s\",\"offset\":%u,"
				 "\"size\":%u,\"crc32\":\"%08x\",\"sha256\":\"%s\",\"replaced\":[%s]}",
				 i ? "," : "", e[i].file, f->name, e[i].role, f->offset, f->size,
				 cd[i].crc, hex, notes);
		if (mlen >= mcap) {
			free(manifest);
			return -ENOMEM;
		}
	}
	mlen += snprintf(manifest + mlen, mcap - mlen, "]}\n");
	if (mlen >= mcap) {
		free(manifest);
		return -ENOMEM;
	}
	add_file(&z, &cd[n], "manifest.json", (const uint8_t *)manifest, mlen);
	free(manifest);

	cd_start = z.off;
	for (unsigned i = 0; i <= n; i++)
		central_entry(&z, &cd[i]);
	memset(eocd, 0, sizeof(eocd));
	le32(eocd, 0x06054b50);
	le16(eocd + 8, n + 1);
	le16(eocd + 10, n + 1);
	le32(eocd + 12, z.off - cd_start);
	le32(eocd + 16, cd_start);
	zput(&z, eocd, sizeof(eocd));
	if (z.err)
		return z.err;
	if (z.off > 0xffffffffu)
		return -EFBIG;
	if (fflush(out))
		return -EIO;
	bb_sha256_final(&z.sha, d);
	bb_sha256_hex(d, r->sha256);
	r->bytes = z.off;
	r->entries = n + 1;
	return 0;
}
