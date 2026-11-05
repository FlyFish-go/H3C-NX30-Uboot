// SPDX-License-Identifier: GPL-2.0+
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t u8;
#include "../include/h3c_fem.h"

static enum h3c_fem_state run(const char *hdr, int a, int b, size_t len)
{
	u8 blk[0x80];
	memset(blk, 0xff, sizeof(blk));
	memcpy(blk, hdr, 4);
	blk[0x3a] = (u8)a;
	blk[0x45] = (u8)b;
	return h3c_fem_classify(blk, len);
}

int main(void)
{
	const char ok[4] = { 1, 0, 4, 4 }, bad[4] = { 1, 0, 4, 5 };
	int n = 0;
	assert(run(ok, 0x60, 0x09, 0x46) == H3C_FEM_PRESENT); n++;
	assert(run(ok, 0x50, 0x01, 0x46) == H3C_FEM_ABSENT); n++;
	assert(run(ok, 0x60, 0x01, 0x46) == H3C_FEM_UNKNOWN); n++;
	assert(run(ok, 0x50, 0x09, 0x46) == H3C_FEM_UNKNOWN); n++;
	assert(run(bad, 0x50, 0x01, 0x46) == H3C_FEM_UNKNOWN); n++;
	assert(run(ok, 0x50, 0x01, 0x45) == H3C_FEM_UNKNOWN); n++;
	assert(h3c_fem_classify(NULL, 0x46) == H3C_FEM_UNKNOWN); n++;
	assert(!strcmp(h3c_fem_group(H3C_FEM_PRESENT), "D")); n++;
	assert(!strcmp(h3c_fem_group(H3C_FEM_ABSENT), "B")); n++;
	assert(!h3c_fem_group(H3C_FEM_UNKNOWN) && !h3c_fem_group(H3C_FEM_NA)); n++;
	printf("h3c_fem_hosttest: %d PASS\n", n);
	return 0;
}
