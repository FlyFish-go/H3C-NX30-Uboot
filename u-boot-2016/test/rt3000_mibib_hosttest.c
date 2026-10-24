// SPDX-License-Identifier: GPL-2.0+
/* rt3000_mibib_hosttest MIBIB_BACKUP: a device MIBIB dump must pass and every
 * single structural defect must fail; also exercises the U-Boot build identity
 * check with the same compile-time identity a real build embeds. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt3000_mibib.h"

typedef uint8_t u8;
const char *h3c_build_id(void);
int h3c_build_id_check(const u8 *image, size_t size);

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static u8 good[RT3_MIBIB_SIZE], img[RT3_MIBIB_SIZE];

static void reset(void) { memcpy(img, good, sizeof(img)); }

static uint32_t crc32_msb(const u8 *p, uint32_t n)
{
	uint32_t crc = 0;
	while (n--) {
		crc ^= (uint32_t)*p++ << 24;
		for (int b = 0; b < 8; b++)
			crc = crc & 0x80000000U ? (crc << 1) ^ 0x04c11db7U : crc << 1;
	}
	return crc;
}

static void put32(u8 *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void refresh_crc(unsigned copy)
{
	u8 *b = img + copy * RT3_MIBIB_BLOCK;
	put32(b + 3 * RT3_MIBIB_PAGE + 12, crc32_msb(b, 3 * RT3_MIBIB_PAGE));
}

int main(int argc, char **argv)
{
	uint32_t ages[2];
	FILE *f;

	CHECK(argc == 2);
	f = fopen(argv[1], "rb");
	CHECK(f && fread(good, 1, sizeof(good), f) == sizeof(good) && fgetc(f) == EOF);
	fclose(f);

	reset(); CHECK(rt3_mibib_check(img, sizeof(img), ages) == 0 && ages[0] != ages[1]);
	CHECK(rt3_mibib_check(img, sizeof(img) - 1, NULL) != 0);
	CHECK(rt3_mibib_check(NULL, sizeof(img), NULL) != 0);

	/* Payload bit flips break the stored CRC in either copy. */
	for (unsigned c = 0; c < 2; c++) {
		reset(); img[c * RT3_MIBIB_BLOCK + RT3_MIBIB_PAGE + 20] ^= 1;
		CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
		reset(); img[c * RT3_MIBIB_BLOCK + 3 * RT3_MIBIB_PAGE + 12] ^= 1;
		CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	}
	/* A consistently re-checksummed copy that differs from the other copy. */
	reset(); img[RT3_MIBIB_BLOCK + RT3_MIBIB_PAGE + 16 + 15 * 28 + 16] ^= 1; refresh_crc(1);
	CHECK(rt3_mibib_block_age(img + RT3_MIBIB_BLOCK, RT3_MIBIB_BLOCK, NULL) == 0);
	CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	/* Equal ages leave SBL no newest copy. */
	reset(); memcpy(img + RT3_MIBIB_BLOCK + 12, img + 12, 4); refresh_crc(1);
	CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	/* Data outside the two copies, in a copy's spare pages, or in spare slots. */
	reset(); img[2 * RT3_MIBIB_BLOCK + 100] = 0; CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	reset(); img[4 * RT3_MIBIB_PAGE + 7] = 0; CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	reset(); img[RT3_MIBIB_PAGE + 16 + 31 * 28] = 'x'; refresh_crc(0);
	CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	/* User table names must follow the SMEM table. */
	reset(); img[2 * RT3_MIBIB_PAGE + 16 + 2] ^= 1; refresh_crc(0);
	CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	/* Header and table magics. */
	reset(); img[0] ^= 1; CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	reset(); img[RT3_MIBIB_PAGE] ^= 1; refresh_crc(0); CHECK(rt3_mibib_check(img, sizeof(img), NULL) != 0);
	puts("MIBIB structure host test: PASS");

	/* Build identity: exactly one equal string inside the candidate. */
	{
		const char *own = h3c_build_id();
		size_t n = strlen(own) + 1;
		u8 buf[4096];

		CHECK(!strncmp(own, "H3C-UBOOT-ID:v1;board=A;profile=h3c-oem-efba-128m-2k-sp64;mask=0x1;ecc=4;", n));
		memset(buf, 0xa5, sizeof(buf));
		CHECK(h3c_build_id_check(buf, sizeof(buf)) == -ENOENT);
		memcpy(buf + 100, own, n);
		CHECK(h3c_build_id_check(buf, sizeof(buf)) == 0);
		memcpy(buf + 2000, own, n);
		CHECK(h3c_build_id_check(buf, sizeof(buf)) == -EEXIST);
		memset(buf + 2000, 0xa5, n);
		buf[100 + 22] = 'B';	/* board=B */
		CHECK(h3c_build_id_check(buf, sizeof(buf)) == -EXDEV);
		memcpy(buf + 100, own, n);
		buf[100 + n - 1] = 'x';	/* longer string sharing the prefix */
		CHECK(h3c_build_id_check(buf, sizeof(buf)) == -EXDEV);
		CHECK(h3c_build_id_check(buf, 100 + n - 1) == -EXDEV);
		puts("U-Boot build identity host test: PASS");
	}
	return 0;
}
