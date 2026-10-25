// SPDX-License-Identifier: GPL-2.0-or-later
#include "pack.h"
#include "sha256.h"

#include "../third_party/lzma/LzmaDec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LZMA_HEADER 13	/* 5 property bytes + u64 uncompressed size */
#define CHUNK 65536

static uint32_t le32(const uint8_t *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

enum bb_pack_status bb_pack_open(const uint8_t *buf, size_t len, struct bb_pack *p)
{
	struct bb_sha256 s;
	uint8_t d[32];
	uint32_t rl, ll;

	memset(p, 0, sizeof(*p));
	if (len < BB_PACK_HEADER || memcmp(buf, "BBPK", 4))
		return BB_PACK_NONE;
	rl = le32(buf + 8);
	ll = le32(buf + 12);
	if (le32(buf + 4) != 1 || rl == 0 || ll < LZMA_HEADER ||
	    rl > len - BB_PACK_HEADER || ll > len - BB_PACK_HEADER - rl ||
	    le32(buf + 16) == 0 || le32(buf + 16) > BB_PACK_MAX_TAR)
		return BB_PACK_BAD;
	bb_sha256_init(&s);
	bb_sha256_update(&s, buf + BB_PACK_HEADER, (size_t)rl + ll);
	bb_sha256_final(&s, d);
	if (memcmp(d, buf + 20, 32))
		return BB_PACK_HASH;
	/* The rule text must not contain NULs: it is parsed as a C string. */
	if (memchr(buf + BB_PACK_HEADER, 0, rl))
		return BB_PACK_BAD;
	p->rules = (const char *)buf + BB_PACK_HEADER;
	p->rules_len = rl;
	p->lzma = buf + BB_PACK_HEADER + rl;
	p->lzma_len = ll;
	p->tar_len = le32(buf + 16);
	return BB_PACK_OK;
}

static void *lz_alloc(void *p, size_t size)
{
	(void)p;
	return malloc(size);
}

static void lz_free(void *p, void *addr)
{
	(void)p;
	free(addr);
}

/* ustar: name[100] at 0, size (octal) at 124, typeflag at 156, magic at 257,
 * prefix[155] at 345. Returns the member size or -1. */
static long tar_member(const uint8_t *h, char *name, size_t name_len)
{
	char size[13];
	char *end;
	long n;

	if (memcmp(h + 257, "ustar", 5))
		return -1;
	if (h[345])
		snprintf(name, name_len, "%.155s/%.100s", (const char *)h + 345, (const char *)h);
	else
		snprintf(name, name_len, "%.100s", (const char *)h);
	memcpy(size, h + 124, 12);
	size[12] = 0;
	n = strtol(size, &end, 8);
	if (end == size || n < 0)
		return -1;
	return n;
}

enum bb_pack_status bb_pack_extract(const struct bb_pack *p, const char *want,
				    uint8_t **out, size_t *out_len)
{
	static ISzAlloc alloc = { lz_alloc, lz_free };
	enum bb_pack_status ret = BB_PACK_DECODE;
	const uint8_t *src = p->lzma + LZMA_HEADER;
	size_t src_left = p->lzma_len - LZMA_HEADER;
	uint8_t hdr[512], *chunk, *file = NULL;
	size_t hdr_used = 0, total = 0;
	long member_left = 0, pad_left = 0, file_size = 0, file_got = 0;
	int copying = 0, zero_blocks = 0;
	CLzmaDec dec;

	*out = NULL;
	*out_len = 0;
	chunk = malloc(CHUNK);
	if (!chunk)
		return BB_PACK_NO_MEM;
	LzmaDec_Construct(&dec);
	if (LzmaDec_Allocate(&dec, p->lzma, 5, &alloc) != SZ_OK) {
		free(chunk);
		return BB_PACK_NO_MEM;
	}
	LzmaDec_Init(&dec);

	while (total < p->tar_len) {
		SizeT dst = CHUNK, in = src_left;
		ELzmaStatus st;

		if (dst > p->tar_len - total)
			dst = p->tar_len - total;
		if (LzmaDec_DecodeToBuf(&dec, chunk, &dst, src, &in, LZMA_FINISH_ANY, &st) != SZ_OK ||
		    (dst == 0 && in == 0))
			goto done;
		src += in;
		src_left -= in;
		total += dst;

		for (size_t i = 0; i < dst;) {
			size_t n;

			if (member_left) {
				n = dst - i < (size_t)member_left ? dst - i : (size_t)member_left;
				if (copying) {
					memcpy(file + file_got, chunk + i, n);
					file_got += n;
				}
				member_left -= n;
				i += n;
				if (!member_left && copying) {
					*out = file;
					*out_len = file_size;
					file = NULL;
					ret = BB_PACK_OK;
					goto done;
				}
				continue;
			}
			if (pad_left) {
				n = dst - i < (size_t)pad_left ? dst - i : (size_t)pad_left;
				pad_left -= n;
				i += n;
				continue;
			}
			n = dst - i < 512 - hdr_used ? dst - i : 512 - hdr_used;
			memcpy(hdr + hdr_used, chunk + i, n);
			hdr_used += n;
			i += n;
			if (hdr_used < 512)
				continue;
			hdr_used = 0;
			{
				static const uint8_t zero[512];
				char name[260];
				long size;

				if (!memcmp(hdr, zero, 512)) {
					if (++zero_blocks == 2) {
						ret = BB_PACK_NOT_FOUND;
						goto done;
					}
					continue;
				}
				zero_blocks = 0;
				size = tar_member(hdr, name, sizeof(name));
				if (size < 0 || (size_t)size > p->tar_len)
					goto done;
				member_left = size;
				pad_left = (512 - size % 512) % 512;
				copying = hdr[156] == '0' && !strcmp(name, want);
				if (copying) {
					file_size = size;
					file = malloc(size ? size : 1);
					if (!file) {
						ret = BB_PACK_NO_MEM;
						goto done;
					}
					if (!size) {
						*out = file;
						file = NULL;
						ret = BB_PACK_OK;
						goto done;
					}
				}
			}
		}
	}
	ret = BB_PACK_NOT_FOUND;
done:
	free(file);
	free(chunk);
	LzmaDec_Free(&dec, &alloc);
	return ret;
}

const char *bb_pack_status_str(enum bb_pack_status s)
{
	switch (s) {
	case BB_PACK_OK: return "ok";
	case BB_PACK_NONE: return "none";
	case BB_PACK_BAD: return "bad";
	case BB_PACK_HASH: return "hash";
	case BB_PACK_NOT_FOUND: return "not_found";
	case BB_PACK_DECODE: return "decode";
	case BB_PACK_NO_MEM: return "no_memory";
	}
	return "unknown";
}
