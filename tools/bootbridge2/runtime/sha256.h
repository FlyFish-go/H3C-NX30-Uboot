/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_SHA256_H
#define BB_SHA256_H

#include <stddef.h>
#include <stdint.h>

struct bb_sha256 {
	uint32_t h[8];
	uint8_t buf[64];
	size_t used;
	uint64_t len;
};

void bb_sha256_init(struct bb_sha256 *s);
void bb_sha256_update(struct bb_sha256 *s, const void *data, size_t len);
void bb_sha256_final(struct bb_sha256 *s, uint8_t out[32]);
void bb_sha256_hex(const uint8_t d[32], char out[65]);

#endif
