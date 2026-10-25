/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Rule pack: what the web page writes into the bridge image.
 *
 *   0   "BBPK"
 *   4   u32 version (1)
 *   8   u32 rules_len      rule file (text), see rules.h
 *   12  u32 lzma_len       .lzma stream of a ustar archive with the candidates
 *   16  u32 tar_len        uncompressed archive size
 *   20  u8[32]             SHA-256 over rules ‖ lzma
 *   52  u8[12]             zero
 *   64  rules, then lzma
 * All integers little endian. */
#ifndef BB_PACK_H
#define BB_PACK_H

#include <stddef.h>
#include <stdint.h>

#define BB_PACK_HEADER 64
#define BB_PACK_MAX_TAR (96u << 20)

struct bb_pack {
	const char *rules;
	size_t rules_len;
	const uint8_t *lzma;
	size_t lzma_len;
	uint32_t tar_len;
};

enum bb_pack_status {
	BB_PACK_OK = 0,
	BB_PACK_NONE,		/* no pack magic: the image carries no rules */
	BB_PACK_BAD,		/* malformed header or sizes */
	BB_PACK_HASH,		/* content hash mismatch */
	BB_PACK_NOT_FOUND,	/* requested file not in the archive */
	BB_PACK_DECODE,		/* LZMA or tar error */
	BB_PACK_NO_MEM,
};

/* Validate a pack held in memory (buf must outlive the pack). */
enum bb_pack_status bb_pack_open(const uint8_t *buf, size_t len, struct bb_pack *p);

/* Stream-decompress and return a copy of one archive member. */
enum bb_pack_status bb_pack_extract(const struct bb_pack *p, const char *name,
				    uint8_t **out, size_t *out_len);

const char *bb_pack_status_str(enum bb_pack_status s);

#endif
