// SPDX-License-Identifier: GPL-2.0+
#include <nx30_d_uboot_core.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

#define IMAGE_BYTES 0x3200U
static u8 image[IMAGE_BYTES];
static u8 target[NX30_D_UBOOT_SLOT_BYTES];
static u8 padded[NX30_D_UBOOT_SLOT_BYTES];
static u8 verify[NX30_D_UBOOT_SLOT_BYTES];
static u64 mock_offset = NX30_D_UBOOT_HIGH_OFFSET;

static void put16(u8 *p, u16 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* Deterministic test digest; production injects the U-Boot SHA-256 routine. */
static void test_digest(const u8 *data, size_t length, u8 out[32], void *ctx)
{
	u32 h = 2166136261U;
	size_t i;
	unsigned int j;
	(void)ctx;
	for (i = 0; i < length; ++i)
		h = (h ^ data[i]) * 16777619U;
	for (j = 0; j < 8; ++j) {
		u32 x = h ^ (0x9e3779b9U * j);
		put32(out + j * 4, x);
	}
}

static void real_sha256(const u8 *data, size_t length, u8 out[32], void *ctx)
{
	(void)ctx;
	assert(SHA256(data, length, out) != NULL);
}

static void test_production_mbn_file(const char *path)
{
	struct nx30_d_uboot_core_info info;
	FILE *stream = fopen(path, "rb");
	u8 *data;
	long length;

	assert(stream != NULL);
	assert(fseek(stream, 0, SEEK_END) == 0);
	length = ftell(stream);
	assert(length > 0 && fseek(stream, 0, SEEK_SET) == 0);
	data = malloc((size_t)length);
	assert(data != NULL);
	assert(fread(data, 1, (size_t)length, stream) == (size_t)length);
	assert(fclose(stream) == 0);
	assert(nx30_d_uboot_parse_mbn(data, (size_t)length, real_sha256, NULL,
				      &info) == 0);
	assert(info.image_bytes == (u32)length);
	assert(info.load_end > NX30_D_UBOOT_ENTRY);
	assert(info.bss_end < NX30_D_UBOOT_BSS_LIMIT);
	free(data);
}

static void phdr(u8 *p, u32 type, u32 off, u32 va, u32 pa,
		 u32 filesz, u32 memsz, u32 flags, u32 align)
{
	put32(p, type); put32(p + 4, off); put32(p + 8, va); put32(p + 12, pa);
	put32(p + 16, filesz); put32(p + 20, memsz); put32(p + 24, flags);
	put32(p + 28, align);
}

static void make_image(void)
{
	u8 *hash = image + 0x1000;
	u8 *payload = image + 0x3000;
	u8 dig_input[52 + 4 * 32];
	u32 i;

	memset(image, 0, sizeof(image));
	memcpy(image, "\x7f" "ELF\x01\x01\x01", 7);
	put16(image + 16, 3); put16(image + 18, 40); put32(image + 20, 1);
	put32(image + 24, NX30_D_UBOOT_ENTRY); put32(image + 28, 52);
	put16(image + 40, 52); put16(image + 42, 32); put16(image + 44, 4);
	phdr(image + 52, 0, 0, 0, 0, 0xb4, 0, 0x07000000, 0);
	phdr(image + 84, 0, 0x1000, 0x4a921000, 0x4a921000,
	     0xa8, 0x1000, 0x02200000, 0x1000);
	phdr(image + 116, 1, 0x3000, NX30_D_UBOOT_ENTRY,
	     NX30_D_UBOOT_ENTRY, 0x200, 0x200, 7, 0x1000);
	phdr(image + 148, 0x6474e551, 0x2000, 0, 0, 0, 0, 7, 0x10);
	for (i = 0; i < 0x200; ++i)
		payload[i] = (u8)(i * 13U + 7U);
	memset(payload, 0, sizeof(struct nx30_d_uboot_identity));
	memcpy(payload, NX30_D_UBOOT_IMAGE_MAGIC,
	       sizeof(NX30_D_UBOOT_IMAGE_MAGIC));
	memcpy(payload + 32, NX30_D_UBOOT_LABEL, sizeof(NX30_D_UBOOT_LABEL));
	memcpy(payload + 64, NX30_D_UBOOT_COMPATIBLE,
	       sizeof(NX30_D_UBOOT_COMPATIBLE));
	memcpy(payload + 80, NX30_D_UBOOT_MODEL, sizeof(NX30_D_UBOOT_MODEL));
	put32(payload + 96, NX30_D_UBOOT_MACHID);
	put32(payload + 100, NX30_D_UBOOT_ECC_BITS);
	put32(payload + 104, 0);
	put32(payload + 108, NX30_D_UBOOT_IDENTITY_FLAGS);
	put32(payload + 112, NX30_D_UBOOT_ENTRY + 0x400);
	put32(hash, 21); put32(hash + 4, 3); put32(hash + 16, 0x80);
	put32(hash + 12, 0x4a922028); put32(hash + 20, 0x80);
	put32(hash + 24, 0x4a9220a8); put32(hash + 32, 0x4a9220a8);
	memcpy(dig_input, image, 52);
	memcpy(dig_input + 52, image + 52, 128);
	test_digest(dig_input, sizeof(dig_input), hash + 40, NULL);
	test_digest(payload, 0x200, hash + 104, NULL);
}

struct mock_io { int opens, closes, erases, writes, reads, corrupt, close_error; };

static int mock_open(void *ctx, u64 off, u64 size, unsigned int ecc)
{
	struct mock_io *m = ctx;
	assert(off == mock_offset);
	assert(size == NX30_D_UBOOT_SLOT_BYTES);
	assert(ecc == 4);
	++m->opens;
	return 0;
}
static void mock_close(void *ctx) { ++((struct mock_io *)ctx)->closes; }
static int mock_close_error(void *ctx)
{
	return ((struct mock_io *)ctx)->close_error;
}
static int mock_erase(void *ctx, u64 off, u64 size)
{
	assert(off == mock_offset && size == sizeof(target));
	memset(target, 0xff, sizeof(target));
	++((struct mock_io *)ctx)->erases;
	return 0;
}
static int mock_write(void *ctx, u64 off, const void *data, size_t size,
		      size_t *written)
{
	assert(off >= mock_offset);
	assert(off + size <= mock_offset + sizeof(target));
	memcpy(target + (off - mock_offset), data, size);
	*written = size;
	++((struct mock_io *)ctx)->writes;
	return 0;
}
static int mock_read(void *ctx, u64 off, void *data, size_t size, size_t *read)
{
	struct mock_io *m = ctx;
	assert(off >= mock_offset);
	assert(off + size <= mock_offset + sizeof(target));
	memcpy(data, target + (off - mock_offset), size);
	if (m->corrupt)
		((u8 *)data)[0] ^= 1;
	*read = size;
	++m->reads;
	return 0;
}

static void test_fixed_string_padding(void)
{
	static const char expected[32] = NX30_D_UBOOT_IMAGE_MAGIC;
	char actual[32] = NX30_D_UBOOT_IMAGE_MAGIC;

	assert(nx30_d_uboot_fixed_string_equal(actual, expected,
						 sizeof(expected)));
	actual[sizeof(actual) - 1] = 'X';
	assert(!nx30_d_uboot_fixed_string_equal(actual, expected,
						  sizeof(expected)));
}

static struct nx30_d_uboot_geometry geometry(void)
{
	struct nx30_d_uboot_geometry g = {
		.generation = 7, .nand_size = 0x08000000,
		.erase_size = 0x20000, .write_size = 0x800, .ecc_bits = 4,
		.appsbl_offset = NX30_D_UBOOT_HIGH_OFFSET,
		.appsbl_size = NX30_D_UBOOT_SLOT_BYTES,
	};
	return g;
}

static void put_bootconfig(u8 *bc, u32 appsbl_selector, u32 rootfs_selector)
{
	memset(bc, 0, 256U << 10);
	put32(bc, 0xa3a2a1a0U);
	put32(bc + 8, 8U);
	memcpy(bc + 0x48, "0:APPSBL", 8);
	put32(bc + 0x58, appsbl_selector);
	put32(bc + 0x80, rootfs_selector);
	put32(bc + 332, 0xb3b2b1b0U);
}

static void test_appsbl_selector_gate(void)
{
	u64 resolved = 0;
	u8 *bc0 = calloc(1, 256U << 10);
	u8 *bc1 = calloc(1, 256U << 10);
	assert(bc0 && bc1);
	put_bootconfig(bc0, 1, 0);
	memcpy(bc1, bc0, 256U << 10);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) == 0 && resolved == NX30_D_UBOOT_HIGH_OFFSET);
	/* rootfs selector does not determine which APPSBL copy is active. */
	put32(bc0 + 0x80, 1);
	put32(bc1 + 0x80, 1);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) == 0 && resolved == NX30_D_UBOOT_HIGH_OFFSET);
	put32(bc0 + 0x58, 0);
	put32(bc1 + 0x58, 0);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_LOW_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) == 0 && resolved == NX30_D_UBOOT_LOW_OFFSET);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) != 0);
	put_bootconfig(bc0, 1, 0);
	put_bootconfig(bc1, 1, 0);
	bc1[0x20000] ^= 1;
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) != 0);
	memcpy(bc1, bc0, 256U << 10);
	put32(bc0 + 332, 0);
	memcpy(bc1, bc0, 256U << 10);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) != 0);
	put_bootconfig(bc0, 1, 0);
	put_bootconfig(bc1, 1, 0);
	put32(bc0, 0);
	memcpy(bc1, bc0, 256U << 10);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) != 0);
	put_bootconfig(bc0, 1, 0);
	put_bootconfig(bc1, 1, 0);
	put32(bc0 + 8, 7);
	memcpy(bc1, bc0, 256U << 10);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) != 0);
	put_bootconfig(bc0, 1, 0);
	put_bootconfig(bc1, 1, 0);
	memset(bc0 + 0x48, 0, 16);
	memcpy(bc1, bc0, 256U << 10);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, 256U << 10, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) != 0);
	assert(nx30_d_uboot_active_copy_resolve(bc0, bc1, (256U << 10) - 1, NX30_D_UBOOT_HIGH_OFFSET, NX30_D_UBOOT_SLOT_BYTES, &resolved) != 0);
	free(bc1);
	free(bc0);
}

int main(int argc, char **argv)
{
	assert(argc <= 2);
	if (argc == 2)
		test_production_mbn_file(argv[1]);
	struct nx30_d_uboot_core_info info;
	struct nx30_d_uboot_geometry g = geometry();
	test_fixed_string_padding();
	test_appsbl_selector_gate();
	struct mock_io mock = {0};
	struct nx30_d_uboot_io io = {
		.context = &mock, .open = mock_open, .close = mock_close,
		.close_error = mock_close_error, .erase = mock_erase,
		.write = mock_write, .read = mock_read,
	};
	u32 verified, stage, crc;

	assert(nx30_d_uboot_runtime_role_matches(NX30_D_UBOOT_SERVICE_RAM));
	assert(!nx30_d_uboot_runtime_role_matches(
		NX30_D_UBOOT_SERVICE_RAM ? 0U : 1U));

	make_image();
	assert(nx30_d_uboot_parse_mbn(image, sizeof(image), test_digest, NULL,
				      &info) == 0);
	/* Other build profiles and dirty fixed-width padding must reject even
	 * after recomputing the executable digest. Never match a string prefix. */
	memset(image + 0x3000 + 32, 0, 32);
#ifdef CONFIG_H3C_RELEASE_SYSTEM_DEFAULT
	memcpy(image + 0x3000 + 32, "BOOTBRIDGE-HW-D", sizeof("BOOTBRIDGE-HW-D"));
#else
	memcpy(image + 0x3000 + 32, "BOOTBRIDGE-HW-D", sizeof("BOOTBRIDGE-HW-D"));
#endif
	test_digest(image + 0x3000, 0x200, image + 0x1000 + 104, NULL);
	assert(nx30_d_uboot_parse_mbn(image, sizeof(image), test_digest, NULL, &info) != 0);
	make_image();
	image[0x3000 + 63] = 1;
	test_digest(image + 0x3000, 0x200, image + 0x1000 + 104, NULL);
	assert(nx30_d_uboot_parse_mbn(image, sizeof(image), test_digest, NULL, &info) != 0);
	make_image();
	assert(nx30_d_uboot_parse_mbn(image, sizeof(image), test_digest, NULL, &info) == 0);
	assert(info.load_end == NX30_D_UBOOT_ENTRY + 0x200);
	assert(info.bss_end == NX30_D_UBOOT_ENTRY + 0x400);
	/* Old header pointers omitted Qualcomm's 0x1000 MBN header base. */
	put32(image + 0x1000 + 12, 0x4a921028);
	put32(image + 0x1000 + 24, 0x4a9210a8);
	put32(image + 0x1000 + 32, 0x4a9210a8);
	assert(nx30_d_uboot_parse_mbn(image, sizeof(image), test_digest, NULL,
				      &info) != 0);
	put32(image + 0x1000 + 12, 0x4a922028);
	put32(image + 0x1000 + 24, 0x4a9220a8);
	put32(image + 0x1000 + 32, 0x4a9220a8);
	crc = nx30_d_uboot_crc32(image, sizeof(image));
	assert(nx30_d_uboot_program_core(image, sizeof(image), g.generation, crc,
					 &g, test_digest, NULL, &io,
					 padded, sizeof(padded), verify,
					 sizeof(verify), &verified, &stage) == 0);
	assert(stage == NX30_D_UBOOT_STAGE_COMPLETE);
	assert(verified == NX30_D_UBOOT_SLOT_BYTES);
	assert(!memcmp(target, image, sizeof(image)));
	assert(target[sizeof(image)] == 0xff);
	assert(mock.opens == 1 && mock.closes == 1 && mock.erases == 1 &&
	       (u32)mock.writes == NX30_D_UBOOT_SLOT_BYTES / g.write_size &&
	       (u32)mock.reads == NX30_D_UBOOT_SLOT_BYTES / g.write_size);

	/* Full readback is not PASS if the write protection failed to restore. */
	mock.close_error = 1;
	assert(nx30_d_uboot_program_core(image, sizeof(image), g.generation, crc,
					 &g, test_digest, NULL, &io, padded,
					 sizeof(padded), verify, sizeof(verify),
					 &verified, &stage) != 0);
	assert(stage == NX30_D_UBOOT_STAGE_PROTECT);
	assert(verified == NX30_D_UBOOT_SLOT_BYTES);
	assert(mock.opens == 2 && mock.closes == 2);
	mock.close_error = 0;

	/* RAM-only images and stale layout generations fail before opening a window. */
	put32(image + 0x3000 + 104, 1);
	/* Keep the digest valid so this rejects the role, not unrelated corruption. */
	test_digest(image + 0x3000, 0x200, image + 0x1000 + 104, NULL);
	assert(nx30_d_uboot_parse_mbn(image, sizeof(image), test_digest, NULL,
				      &info) != 0);
	assert(nx30_d_uboot_program_core(image, sizeof(image), g.generation,
					 nx30_d_uboot_crc32(image, sizeof(image)),
					 &g, test_digest, NULL, &io, padded,
					 sizeof(padded), verify, sizeof(verify),
					 &verified, &stage) != 0);
	assert(mock.opens == 2 && mock.closes == 2);
	put32(image + 0x3000 + 104, 0);
	/* restore the load hash after modifying the descriptor */
	test_digest(image + 0x3000, 0x200, image + 0x1000 + 104, NULL);
	assert(nx30_d_uboot_program_core(image, sizeof(image), g.generation,
					 crc ^ 1U, &g, test_digest, NULL, &io, padded,
					 sizeof(padded), verify, sizeof(verify),
					 &verified, &stage) != 0);
	assert(mock.opens == 2);
	assert(nx30_d_uboot_program_core(image, sizeof(image), g.generation + 1,
					 crc, &g, test_digest, NULL, &io, padded,
					 sizeof(padded), verify, sizeof(verify),
					 &verified, &stage) != 0);
	assert(mock.opens == 2);
	crc = nx30_d_uboot_crc32(image, sizeof(image));
	mock.corrupt = 1;
	assert(nx30_d_uboot_program_core(image, sizeof(image), g.generation, crc,
					 &g, test_digest, NULL, &io, padded,
					 sizeof(padded), verify, sizeof(verify),
					 &verified, &stage) != 0);
	assert(stage == NX30_D_UBOOT_STAGE_READBACK && verified == 0);
	assert(mock.opens == 3 && mock.closes == 3);

	mock.corrupt = 0;
	g.appsbl_offset = NX30_D_UBOOT_LOW_OFFSET;
	mock_offset = NX30_D_UBOOT_LOW_OFFSET;
	assert(nx30_d_uboot_program_core(image, sizeof(image), g.generation, crc,
					 &g, test_digest, NULL, &io, padded,
					 sizeof(padded), verify, sizeof(verify),
					 &verified, &stage) == 0);
	assert(stage == NX30_D_UBOOT_STAGE_COMPLETE && verified == NX30_D_UBOOT_SLOT_BYTES);
	puts("LOW and HIGH active-range writer core: PASS");
	puts("nx30_d_uboot_core_test: PASS");
	return 0;
}
