// SPDX-License-Identifier: GPL-2.0+
/* Offline-testable, fail-closed NX30 HW-D APPSBL update core. */
#include <nx30_d_uboot_core.h>

#ifdef NX30_D_UBOOT_HOST_TEST
#include <string.h>
#else
#include <common.h>
#endif

#define MBN_PHDRS 4U
#define MBN_HASH_OFFSET 0x1000U
#define MBN_LOAD_OFFSET 0x3000U
#define MBN_HASH_BYTES 0xa8U
#define MBN_HEADER_SIZE 0x1000U
#define ELF_HEADER_BYTES 52U
#define ELF_PHDR_BYTES 32U
#define ELF_MACHINE_ARM 40U
#define ELF_ET_DYN 3U
#define MBN_IMAGE_ID_APPSBL 21U
#define MBN_VERSION 3U
#define MBN_HASH_CODE_BYTES 0x80U
#define MBN_GNU_STACK 0x6474e551U

static u16 get_le16(const u8 *p)
{
	return (u16)p[0] | ((u16)p[1] << 8);
}

static u32 get_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

int nx30_d_uboot_runtime_role_matches(u32 descriptor_role)
{
	return descriptor_role == NX30_D_UBOOT_SERVICE_RAM;
}

/* Resolve the APPSBL selector only when both complete BC copies agree and
 * SMEM points at the selected canonical range. Rootfs selector is unrelated. */
int nx30_d_uboot_active_copy_resolve(const void *input0, const void *input1,
				     size_t length, u64 smem_offset,
				     u64 smem_size, u64 *active_offset)
{
	static const u8 appsbl_name[16] = {
		'0', ':', 'A', 'P', 'P', 'S', 'B', 'L', 0, 0, 0, 0, 0, 0, 0, 0
	};
	const u8 *bc0 = input0;
	u32 selector;
	u64 expected;

	if (!bc0 || !input1 || !active_offset || length != (256U << 10) ||
	    memcmp(bc0, input1, length) ||
	    get_le32(bc0) != 0xa3a2a1a0U || get_le32(bc0 + 8) != 8U ||
	    get_le32(bc0 + 332) != 0xb3b2b1b0U ||
	    memcmp(bc0 + 0x48, appsbl_name, sizeof(appsbl_name)))
		return -1;
	selector = get_le32(bc0 + 0x58);
	if (selector > 1U || smem_size != NX30_D_UBOOT_SLOT_BYTES)
		return -1;
	expected = selector ? NX30_D_UBOOT_HIGH_OFFSET : NX30_D_UBOOT_LOW_OFFSET;
	if (smem_offset != expected)
		return -1;
	*active_offset = expected;
	return 0;
}

int nx30_d_uboot_fixed_string_equal(const volatile char *actual,
					 const char *expected, size_t length)
{
	size_t i;

	if (!actual || !expected)
		return 0;
	for (i = 0; i < length; ++i)
		if (actual[i] != expected[i])
			return 0;
	return 1;
}

u32 nx30_d_uboot_crc32(const void *data, size_t length)
{
	const u8 *p = data;
	u32 crc = 0xffffffffU;
	size_t i;
	unsigned int bit;

	if (!data && length)
		return 0;
	for (i = 0; i < length; ++i) {
		crc ^= p[i];
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
	}
	return ~crc;
}

static int phdr(const u8 *image, size_t length, u32 table, u32 index,
		u32 out[8])
{
	u32 i;
	size_t off = (size_t)table + (size_t)index * ELF_PHDR_BYTES;

	if (index >= MBN_PHDRS || off > length || ELF_PHDR_BYTES > length - off)
		return -1;
	for (i = 0; i < 8; ++i)
		out[i] = get_le32(image + off + i * 4U);
	return 0;
}

static int descriptor_valid(const u8 *load, size_t load_bytes, u32 load_end,
			    u32 *bss_end)
{
	static const struct nx30_d_uboot_identity expected = {
		.magic = NX30_D_UBOOT_IMAGE_MAGIC,
		.sample = NX30_D_UBOOT_LABEL,
		.compatible = NX30_D_UBOOT_COMPATIBLE,
		.model = NX30_D_UBOOT_MODEL,
		.machid = NX30_D_UBOOT_MACHID,
		.ecc_bits = NX30_D_UBOOT_ECC_BITS,
		.service_ram = NX30_D_UBOOT_SERVICE_RAM,
		.flags = NX30_D_UBOOT_IDENTITY_FLAGS,
	};
	size_t pos;
	unsigned int found = 0;
	u32 candidate_bss = 0;

	if (load_bytes < sizeof(expected))
		return -1;
	for (pos = 0; pos <= load_bytes - sizeof(expected); ++pos) {
		if (!memcmp(load + pos, expected.magic, sizeof(expected.magic))) {
			const u8 *candidate = load + pos;
			u32 end;

			/* Ignore magic copies in code/constants; count full descriptors. */
			if (memcmp(candidate + 32, expected.sample,
				   sizeof(expected.sample)) ||
			    memcmp(candidate + 64, expected.compatible,
				   sizeof(expected.compatible)) ||
			    memcmp(candidate + 80, expected.model,
				   sizeof(expected.model)) ||
			    get_le32(candidate + 96) != NX30_D_UBOOT_MACHID ||
			    get_le32(candidate + 100) != NX30_D_UBOOT_ECC_BITS ||
			    get_le32(candidate + 104) !=
				NX30_D_UBOOT_REQUIRED_SERVICE_RAM ||
			    get_le32(candidate + 108) != NX30_D_UBOOT_IDENTITY_FLAGS)
				continue;
			end = get_le32(candidate + 112);
			if (end <= load_end || end >= NX30_D_UBOOT_BSS_LIMIT)
				continue;
			candidate_bss = end;
			++found;
		}
	}
	if (found != 1)
		return -1;
	*bss_end = candidate_bss;
	return 0;
}

int nx30_d_uboot_parse_mbn(const void *input, size_t length,
			   nx30_d_uboot_digest_fn digest, void *digest_context,
			   struct nx30_d_uboot_core_info *info)
{
	const u8 *image = input;
	u8 digest_input[ELF_HEADER_BYTES + MBN_PHDRS * ELF_PHDR_BYTES];
	u8 expected[32];
	u32 table, ph[MBN_PHDRS][8], elf_entry, load_end, bss_end, hash_va;
	u32 hash_offset, load_offset, load_bytes, i;
	size_t hash_end;

	if (!image || !digest || !info || length > NX30_D_UBOOT_SLOT_BYTES ||
	    length < MBN_LOAD_OFFSET || length < 0x2000U + MBN_HASH_BYTES)
		return -1;
	if (memcmp(image, "\x7f" "ELF\x01\x01\x01", 7) ||
	    get_le16(image + 16) != ELF_ET_DYN ||
	    get_le16(image + 18) != ELF_MACHINE_ARM ||
	    get_le32(image + 20) != 1U)
		return -1;
	elf_entry = get_le32(image + 24);
	table = get_le32(image + 28);
	if (elf_entry != NX30_D_UBOOT_ENTRY ||
	    get_le16(image + 40) != ELF_HEADER_BYTES ||
	    get_le16(image + 42) != ELF_PHDR_BYTES ||
	    get_le16(image + 44) != MBN_PHDRS ||
	    table != ELF_HEADER_BYTES ||
	    table > length || MBN_PHDRS * ELF_PHDR_BYTES > length - table)
		return -1;
	for (i = 0; i < MBN_PHDRS; ++i)
		if (phdr(image, length, table, i, ph[i]))
			return -1;

	/* Match the OEM four-record container shape, including the pseudo-header. */
	if (ph[0][0] != 0 || ph[0][1] != 0 || ph[0][2] != 0 || ph[0][3] != 0 ||
	    ph[0][4] != 0xb4U || ph[0][5] != 0 || ph[0][6] != 0x07000000U ||
	    ph[0][7] != 0 ||
	    ph[1][0] != 0 || ph[1][1] != MBN_HASH_OFFSET ||
	    ph[1][4] != MBN_HASH_BYTES || ph[1][5] != 0x1000U ||
	    ph[1][6] != 0x02200000U || ph[1][7] != 0x1000U ||
	    ph[2][0] != 1 || ph[2][1] != MBN_LOAD_OFFSET ||
	    ph[2][2] != NX30_D_UBOOT_ENTRY || ph[2][3] != NX30_D_UBOOT_ENTRY ||
	    !ph[2][4] || ph[2][4] != ph[2][5] || ph[2][6] != 7U ||
	    ph[2][7] != 0x1000U ||
	    ph[3][0] != MBN_GNU_STACK || ph[3][1] != 0x2000U ||
	    ph[3][2] != 0 || ph[3][3] != 0 || ph[3][4] != 0 || ph[3][5] != 0 ||
	    ph[3][6] != 7U || ph[3][7] != 0x10U)
		return -1;

	hash_offset = ph[1][1];
	load_offset = ph[2][1];
	load_bytes = ph[2][4];
	if (load_offset > length || load_bytes > length - load_offset)
		return -1;
	load_end = NX30_D_UBOOT_ENTRY + load_bytes;
	if (load_end < NX30_D_UBOOT_ENTRY || load_end >= NX30_D_UBOOT_BSS_LIMIT)
		return -1;
	hash_va = (((load_end - 1U) & ~0xfffU) + 0x1000U);
	if (hash_va < load_end || hash_va >= NX30_D_UBOOT_BSS_LIMIT ||
	    ph[1][2] != hash_va ||
	    ph[1][3] != ph[1][2] ||
	    descriptor_valid(image + load_offset, load_bytes, load_end, &bss_end))
		return -1;
	if ((size_t)load_offset + load_bytes > length)
		return -1;
	for (i = load_offset + load_bytes; i < length; ++i)
		if (image[i] != 0xff)
			return -1;

	/* v3 header: APPSBL image ID, 128-byte code hash table, unsigned only. */
	hash_end = (size_t)hash_offset + MBN_HASH_BYTES;
	if (hash_end > length || get_le32(image + hash_offset) != MBN_IMAGE_ID_APPSBL ||
	    get_le32(image + hash_offset + 4) != MBN_VERSION ||
	    get_le32(image + hash_offset + 8) != 0 ||
	    get_le32(image + hash_offset + 12) !=
		hash_va + MBN_HEADER_SIZE + 0x28U ||
	    get_le32(image + hash_offset + 16) != MBN_HASH_CODE_BYTES ||
	    get_le32(image + hash_offset + 20) != MBN_HASH_CODE_BYTES ||
	    get_le32(image + hash_offset + 24) !=
		hash_va + MBN_HEADER_SIZE + MBN_HASH_BYTES ||
	    get_le32(image + hash_offset + 28) != 0 ||
	    get_le32(image + hash_offset + 32) !=
		hash_va + MBN_HEADER_SIZE + MBN_HASH_BYTES ||
	    get_le32(image + hash_offset + 36) != 0)
		return -1;

	memcpy(digest_input, image, ELF_HEADER_BYTES);
	memcpy(digest_input + ELF_HEADER_BYTES, image + table,
	       MBN_PHDRS * ELF_PHDR_BYTES);
	digest(digest_input, sizeof(digest_input), expected, digest_context);
	if (memcmp(image + hash_offset + 40, expected, 32))
		return -1;
	for (i = 0; i < 32; ++i)
		if (image[hash_offset + 72 + i] || image[hash_offset + 136 + i])
			return -1;
	digest(image + load_offset, load_bytes, expected, digest_context);
	if (memcmp(image + hash_offset + 104, expected, 32))
		return -1;

	info->image_bytes = length;
	info->load_bytes = load_bytes;
	info->load_end = load_end;
	info->bss_end = bss_end;
	info->image_crc32 = nx30_d_uboot_crc32(image, length);
	return 0;
}

int nx30_d_uboot_program_core(const void *input, size_t length,
			      u32 expected_generation, u32 expected_crc32,
			      const struct nx30_d_uboot_geometry *geometry,
			      nx30_d_uboot_digest_fn digest, void *digest_context,
			      const struct nx30_d_uboot_io *io,
			      void *partition_buffer, size_t partition_capacity,
			      void *verify_buffer, size_t verify_capacity,
			      u32 *bytes_verified, u32 *stage)
{
	const u8 *image = input;
	u8 *padded = partition_buffer;
	u8 *verify = verify_buffer;
	struct nx30_d_uboot_core_info parsed;
	size_t done, offset, chunk, i;
	int rc = -1, opened = 0;

	if (bytes_verified)
		*bytes_verified = 0;
	if (stage)
		*stage = NX30_D_UBOOT_STAGE_VALIDATE;
	if (!image || !geometry || !io || !io->open || !io->close ||
	    !io->close_error || !io->erase ||
	    !io->write || !io->read || !partition_buffer || !verify_buffer ||
	    !bytes_verified || !stage || !digest ||
	    expected_generation != geometry->generation ||
	    (geometry->appsbl_offset != NX30_D_UBOOT_LOW_OFFSET &&
	     geometry->appsbl_offset != NX30_D_UBOOT_HIGH_OFFSET) ||
	    geometry->appsbl_size != NX30_D_UBOOT_SLOT_BYTES ||
	    geometry->appsbl_offset > geometry->nand_size ||
	    geometry->appsbl_size > geometry->nand_size - geometry->appsbl_offset ||
	    geometry->ecc_bits != NX30_D_UBOOT_ECC_BITS ||
	    geometry->erase_size == 0 || geometry->write_size == 0 ||
	    geometry->appsbl_size % geometry->erase_size ||
	    geometry->appsbl_size % geometry->write_size ||
	    partition_capacity < length || verify_capacity < geometry->write_size ||
	    length > NX30_D_UBOOT_SLOT_BYTES ||
	    length < MBN_LOAD_OFFSET)
		return -1;

	/* Keep a private candidate snapshot; write/readback use only this copy. */
	memcpy(padded, image, length);
	if (nx30_d_uboot_crc32(padded, length) != expected_crc32 ||
	    nx30_d_uboot_parse_mbn(padded, length, digest, digest_context, &parsed) ||
	    parsed.image_crc32 != expected_crc32)
		return -1;
	*stage = NX30_D_UBOOT_STAGE_WINDOW;
	if (io->open(io->context, geometry->appsbl_offset,
		     geometry->appsbl_size, NX30_D_UBOOT_ECC_BITS))
		return -1;
	opened = 1;
	*stage = NX30_D_UBOOT_STAGE_ERASE;
	if (io->erase(io->context, geometry->appsbl_offset,
		      geometry->appsbl_size))
		goto out;
	*stage = NX30_D_UBOOT_STAGE_WRITE;
	chunk = geometry->write_size;
	for (offset = 0; offset < geometry->appsbl_size; offset += chunk) {
		memset(verify, 0xff, chunk);
		if (offset < length) {
			size_t copy = length - offset;
			if (copy > chunk)
				copy = chunk;
			memcpy(verify, padded + offset, copy);
		}
		done = 0;
		if (io->write(io->context, geometry->appsbl_offset + offset,
			      verify, chunk, &done) || done != chunk)
			goto out;
	}
	*stage = NX30_D_UBOOT_STAGE_READBACK;
	for (offset = 0; offset < geometry->appsbl_size; offset += chunk) {
		done = 0;
		if (io->read(io->context, geometry->appsbl_offset + offset,
			     verify, chunk, &done) || done != chunk)
			goto out;
		for (i = 0; i < chunk; ++i) {
			u8 expected = offset + i < length ? padded[offset + i] : 0xff;
			if (verify[i] != expected)
				goto out;
		}
		*bytes_verified += chunk;
	}
	*stage = NX30_D_UBOOT_STAGE_COMPLETE;
	rc = 0;
out:
	if (opened) {
		io->close(io->context);
		if (!rc && io->close_error(io->context)) {
			*stage = NX30_D_UBOOT_STAGE_PROTECT;
			rc = -1;
		}
	}
	return rc;
}

#ifndef NX30_D_UBOOT_HOST_TEST
#include <errno.h>
#include <libfdt.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <asm/global_data.h>
#include <asm-generic/sections.h>
#include <nx30_d_service.h>
#include <nx30_nor_nand.h>
#include <h3c_nand_ecc.h>
#include <u-boot/sha256.h>

DECLARE_GLOBAL_DATA_PTR;

#define NX30_D_SBL1_BYTES 0x0002b800U
#define NX30_D_SBL1_READ_WINDOW_BYTES 0x00010000U

#define NX30_D_APPSBL_BC_BYTES (256U << 10)
#define NX30_D_APPSBL_BC_WINDOW (64U << 10)

/* Resolve the live target from full BC readback and a stable SMEM layout. */
int nx30_d_uboot_active_target(u64 *offset, u64 *size)
{
	struct nx30_d_layout before, after;
	static const enum nx30_d_object objects[] = {
		NX30_D_OBJ_BC0, NX30_D_OBJ_BC1
	};
	u8 *copies[2] = { NULL, NULL };
	unsigned int i;
	u32 pos;
	u64 resolved;
	int ret = -EIO;

	if (!offset || !size || nx30_d_layout_get(&before) ||
	    before.bc0.size != NX30_D_APPSBL_BC_BYTES ||
	    before.bc1.size != NX30_D_APPSBL_BC_BYTES)
		return -EPROTO;
	copies[0] = malloc(NX30_D_APPSBL_BC_BYTES);
	copies[1] = malloc(NX30_D_APPSBL_BC_BYTES);
	if (!copies[0] || !copies[1]) {
		ret = -ENOMEM;
		goto out;
	}
	for (i = 0; i < ARRAY_SIZE(objects); ++i) {
		for (pos = 0; pos < NX30_D_APPSBL_BC_BYTES;
		     pos += NX30_D_APPSBL_BC_WINDOW) {
			size_t got = 0;

			ret = nx30_d_read_object(objects[i], pos, copies[i] + pos,
						 NX30_D_APPSBL_BC_WINDOW, &got);
			if (ret || got != NX30_D_APPSBL_BC_WINDOW) {
				ret = -EIO;
				goto out;
			}
		}
	}
	ret = nx30_d_layout_get(&after);
	if (ret || before.generation != after.generation ||
	    before.bc0.offset != after.bc0.offset ||
	    before.bc1.offset != after.bc1.offset ||
	    before.bc0.size != after.bc0.size ||
	    before.bc1.size != after.bc1.size ||
	    before.appsbl_active.offset != after.appsbl_active.offset ||
	    before.appsbl_active.size != after.appsbl_active.size ||
	    before.appsbl_high.offset != NX30_D_UBOOT_HIGH_OFFSET ||
	    before.appsbl_high.size != NX30_D_UBOOT_SLOT_BYTES ||
	    nx30_d_uboot_active_copy_resolve(copies[0], copies[1],
					     NX30_D_APPSBL_BC_BYTES,
					     before.appsbl_active.offset,
					     before.appsbl_active.size,
					     &resolved)) {
		ret = -EPERM;
		goto out;
	}
	*offset = resolved;
	*size = before.appsbl_active.size;
	ret = 0;
out:
	free(copies[1]);
	free(copies[0]);
	return ret;
}

int nx30_d_uboot_target_check(u64 offset, u64 size)
{
	u64 active_offset, active_size;

	if (nx30_d_uboot_active_target(&active_offset, &active_size) ||
	    offset != active_offset || size != active_size)
		return -EXDEV;
	return 0;
}

/* Matches the current OEM R012 SBL1 boot chain, not merely the SoC/NAND ID. */
static const u8 nx30_d_r012_sbl1_sha256[32] = {
	0xc8, 0xbc, 0x5e, 0x1d, 0x35, 0xa3, 0x09, 0x2d,
	0xc1, 0x29, 0x2d, 0x1b, 0x8b, 0xa1, 0x30, 0x61,
	0x08, 0x2e, 0x08, 0x99, 0x2a, 0x2c, 0xb0, 0x7c,
	0x05, 0xdb, 0xf7, 0x56, 0x1a, 0x12, 0x44, 0xb5,
};

/* Referenced by the production gate; RAM-service builds carry marker 1. */
const volatile struct nx30_d_uboot_identity nx30_d_uboot_image_identity
	__attribute__((used)) = {
	.magic = NX30_D_UBOOT_IMAGE_MAGIC,
	.sample = NX30_D_UBOOT_LABEL,
	.compatible = NX30_D_UBOOT_COMPATIBLE,
	.model = NX30_D_UBOOT_MODEL,
	.machid = NX30_D_UBOOT_MACHID,
	.ecc_bits = NX30_D_UBOOT_ECC_BITS,
	.service_ram = NX30_D_UBOOT_SERVICE_RAM,
	.flags = NX30_D_UBOOT_IDENTITY_FLAGS,
	.bss_end = (u32)&__bss_end,
};


static int nx30_d_embedded_identity_gate(void)
{
	static const struct nx30_d_uboot_identity expected = {
		.magic = NX30_D_UBOOT_IMAGE_MAGIC,
		.sample = NX30_D_UBOOT_LABEL,
		.compatible = NX30_D_UBOOT_COMPATIBLE,
		.model = NX30_D_UBOOT_MODEL,
		.machid = NX30_D_UBOOT_MACHID,
		.ecc_bits = NX30_D_UBOOT_ECC_BITS,
		.service_ram = NX30_D_UBOOT_SERVICE_RAM,
		.flags = NX30_D_UBOOT_IDENTITY_FLAGS,
	};
	const volatile struct nx30_d_uboot_identity *identity =
		&nx30_d_uboot_image_identity;
	u32 bss_end = (u32)&__bss_end;

	if (!nx30_d_uboot_fixed_string_equal(identity->magic, expected.magic,
					     sizeof(expected.magic)) ||
	    !nx30_d_uboot_fixed_string_equal(identity->sample, expected.sample,
					     sizeof(expected.sample)) ||
	    !nx30_d_uboot_fixed_string_equal(identity->compatible,
					     expected.compatible,
					     sizeof(expected.compatible)) ||
	    !nx30_d_uboot_fixed_string_equal(identity->model, expected.model,
					     sizeof(expected.model)) ||
	    identity->machid != expected.machid ||
	    identity->ecc_bits != expected.ecc_bits ||
	    !nx30_d_uboot_runtime_role_matches(identity->service_ram) ||
	    identity->service_ram != expected.service_ram ||
	    identity->flags != expected.flags ||
	    identity->bss_end != bss_end || bss_end <= NX30_D_UBOOT_ENTRY ||
	    bss_end >= NX30_D_UBOOT_BSS_LIMIT)
		return -ENODEV;
	return 0;
}

static void nx30_d_sha256(const u8 *data, size_t length, u8 out[32],
			  void *context)
{
	(void)context;
	sha256_csum_wd(data, (unsigned int)length, out, CHUNKSZ);
}

static int nx30_d_board_gate(void)
{
	const void *fdt = gd ? gd->fdt_blob : NULL;
	const char *model, *host_name, *family;
	const fdt32_t *fdt_machid;
	int root, len;

	if (!fdt || !gd->bd || gd->bd->bi_arch_number != NX30_D_UBOOT_MACHID)
		return -ENODEV;
	if (nx30_d_embedded_identity_gate())
		return -ENODEV;
	root = fdt_path_offset(fdt, "/");
	if (root < 0 || fdt_node_check_compatible(fdt, root,
						 NX30_D_UBOOT_COMPATIBLE))
		return -ENODEV;
	model = fdt_getprop(fdt, root, "model", &len);
	if (!model || len <= 0 || (size_t)len != sizeof(NX30_D_UBOOT_MODEL) ||
	    memcmp(model, NX30_D_UBOOT_MODEL, sizeof(NX30_D_UBOOT_MODEL)))
		return -ENODEV;
	host_name = fdt_getprop(fdt, root, "host_name", &len);
	if (!host_name || len <= 0 ||
	    (size_t)len != sizeof(NX30_D_UBOOT_LABEL) ||
	    memcmp(host_name, NX30_D_UBOOT_LABEL, sizeof(NX30_D_UBOOT_LABEL)))
		return -ENODEV;
	family = fdt_getprop(fdt, root, "h3c,board-family", &len);
	if (!family || len != sizeof("HW-D") || memcmp(family, "HW-D", sizeof("HW-D")))
		return -ENODEV;
	fdt_machid = fdt_getprop(fdt, root, "machid", &len);
	if (!fdt_machid || len != sizeof(*fdt_machid) ||
	    fdt32_to_cpu(*fdt_machid) != NX30_D_UBOOT_MACHID)
		return -ENODEV;
	return 0;
}

static int nx30_d_sbl1_gate(struct mtd_info *mtd)
{
	u8 actual[32];
	u8 *buffer;
	size_t read = 0;
	u32 failed_before, failed_after;
	size_t offset, chunk;
	int ret;

	if (!mtd || mtd->size < NX30_D_SBL1_BYTES || mtd->writesize != 0x800)
		return -ENODEV;
	ret = h3c_nand_ecc_select_mode(NX30_D_UBOOT_ECC_BITS);
	if (ret)
		return ret;
	buffer = malloc(NX30_D_SBL1_BYTES);
	if (!buffer)
		return -ENOMEM;
	failed_before = mtd->ecc_stats.failed;
	for (offset = 0; offset < NX30_D_SBL1_BYTES; offset += chunk) {
		chunk = NX30_D_SBL1_BYTES - offset;
		if (chunk > NX30_D_SBL1_READ_WINDOW_BYTES)
			chunk = NX30_D_SBL1_READ_WINDOW_BYTES;
		read = 0;
		ret = mtd_read(mtd, offset, chunk, &read, buffer + offset);
		if ((ret && ret != -EUCLEAN) || read != chunk) {
			free(buffer);
			return -EIO;
		}
	}
	failed_after = mtd->ecc_stats.failed;
	if (failed_after != failed_before) {
		free(buffer);
		return -EIO;
	}
	sha256_csum_wd(buffer, NX30_D_SBL1_BYTES, actual, CHUNKSZ);
	free(buffer);
	return memcmp(actual, nx30_d_r012_sbl1_sha256, sizeof(actual)) ?
		-EPERM : 0;
}

struct nx30_d_uboot_mtd_context { struct mtd_info *mtd; };

static int nx30_d_mtd_open(void *context, u64 offset, u64 size,
			   unsigned int ecc_bits)
{
	struct nx30_d_uboot_mtd_context *c = context;
	if (nx30_d_uboot_target_check(offset,size)) return -EXDEV;
	return nx30_d_qpic_write_window_open(c->mtd, offset, size, ecc_bits);
}

static void nx30_d_mtd_close(void *context)
{
	(void)context;
	nx30_d_qpic_write_window_close();
}

static int nx30_d_mtd_close_error(void *context)
{
	(void)context;
	return nx30_d_qpic_write_window_close_error();
}

static int nx30_d_mtd_erase(void *context, u64 offset, u64 size)
{
	struct nx30_d_uboot_mtd_context *c = context;
	struct erase_info erase;

	if (!nx30_d_qpic_write_window_allows(c->mtd, offset, size))
		return -EPERM;
	memset(&erase, 0, sizeof(erase));
	erase.mtd = c->mtd;
	erase.addr = offset;
	erase.len = size;
	return mtd_erase(c->mtd, &erase);
}

static int nx30_d_mtd_write(void *context, u64 offset, const void *data,
			    size_t size, size_t *written)
{
	struct nx30_d_uboot_mtd_context *c = context;

	if (!nx30_d_qpic_write_window_allows(c->mtd, offset, size))
		return -EPERM;
	return mtd_write(c->mtd, offset, size, written, data);
}

static int nx30_d_mtd_read(void *context, u64 offset, void *data,
			   size_t size, size_t *read)
{
	struct nx30_d_uboot_mtd_context *c = context;
	int ret = mtd_read(c->mtd, offset, size, read, data);

	return ret == -EUCLEAN && *read == size ? 0 : ret;
}

int nx30_d_uboot_validate(const void *image, size_t length,
			  struct nx30_d_firmware_info *firmware)
{
	struct nx30_d_layout layout;
	struct mtd_info *mtd = &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
	struct nx30_d_uboot_core_info parsed;
	u64 active_offset, active_size;
	int ret;

	if (!image || !firmware)
		return -EINVAL;
	ret = nx30_d_uboot_active_target(&active_offset, &active_size);
	if (ret || nx30_d_uboot_target_check(active_offset, active_size))
		return -EPERM;
	if (length > NX30_D_UBOOT_SLOT_BYTES)
		return -EFBIG;
	ret = nx30_d_board_gate();
	if (ret)
		return ret;
	ret = nx30_d_layout_get(&layout);
	if (ret)
		return ret;
	if (layout.ecc_bits != NX30_D_UBOOT_ECC_BITS ||
	    layout.appsbl_high.offset != NX30_D_UBOOT_HIGH_OFFSET ||
	    layout.appsbl_high.size != NX30_D_UBOOT_SLOT_BYTES)
		return -EPROTO;
	if (!h3c_nand_ecc_write_allowed(mtd, active_offset, active_size))
		return -EPERM;
	ret = nx30_d_sbl1_gate(mtd);
	if (ret)
		return ret;
	ret = nx30_d_uboot_parse_mbn(image, length, nx30_d_sha256, NULL, &parsed);
	if (ret)
		return -EINVAL;
	memset(firmware, 0, sizeof(*firmware));
	firmware->generation = layout.generation;
	firmware->image_crc32 = parsed.image_crc32;
	firmware->image_bytes = parsed.image_bytes;
	snprintf(firmware->release, sizeof(firmware->release),
		 "NX30-D-SERVICE-V1");
	return 0;
}

int nx30_d_uboot_write(const void *image, size_t length,
		       u32 expected_generation, u32 expected_crc32,
		       struct nx30_d_result *result)
{
	struct nx30_d_layout layout;
	struct nx30_d_firmware_info firmware;
	struct nx30_d_uboot_geometry geometry;
	struct nx30_d_uboot_mtd_context context;
	struct nx30_d_uboot_io io;
	u64 active_offset = 0, active_size = 0;
	struct mtd_info *mtd = &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
	void *snapshot = NULL, *page = NULL;
	u32 verified = 0, stage = NX30_D_UBOOT_STAGE_VALIDATE;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	ret = nx30_d_uboot_validate(image, length, &firmware);
	if (ret)
		goto done;
	if (firmware.generation != expected_generation ||
	    firmware.image_crc32 != expected_crc32) {
		ret = -ESTALE;
		goto done;
	}
	ret = nx30_d_layout_get(&layout);
	if (ret)
		goto done;
	if (layout.generation != expected_generation ||
	    layout.ecc_bits != NX30_D_UBOOT_ECC_BITS ||
	    layout.appsbl_high.offset != NX30_D_UBOOT_HIGH_OFFSET ||
	    layout.appsbl_high.size != NX30_D_UBOOT_SLOT_BYTES ||
	    nx30_d_uboot_active_target(&active_offset, &active_size)) {
		ret = -ESTALE;
		goto done;
	}
	if (nx30_d_uboot_target_check(active_offset, active_size)) {
		ret = -EPERM;
		goto done;
	}
	ret = nx30_d_board_gate();
	if (ret)
		goto done;
	if (!h3c_nand_ecc_write_allowed(mtd, active_offset, active_size)) {
		ret = -EPERM;
		goto done;
	}
	ret = nx30_d_sbl1_gate(mtd);
	if (ret)
		goto done;
	snapshot = malloc(length);
	page = malloc(layout.write_size);
	if (!snapshot || !page) {
		ret = -ENOMEM;
		goto done;
	}
	geometry.generation = layout.generation;
	geometry.nand_size = layout.nand_size;
	geometry.erase_size = layout.erase_size;
	geometry.write_size = layout.write_size;
	geometry.ecc_bits = layout.ecc_bits;
	geometry.appsbl_offset = active_offset;
	geometry.appsbl_size = active_size;
	context.mtd = mtd;
	io.context = &context;
	io.open = nx30_d_mtd_open;
	io.close = nx30_d_mtd_close;
	io.close_error = nx30_d_mtd_close_error;
	io.erase = nx30_d_mtd_erase;
	io.write = nx30_d_mtd_write;
	io.read = nx30_d_mtd_read;
	ret = nx30_d_uboot_program_core(image, length, expected_generation,
					expected_crc32, &geometry, nx30_d_sha256,
					NULL, &io, snapshot, length, page,
					layout.write_size, &verified, &stage);
	if (stage == NX30_D_UBOOT_STAGE_PROTECT)
		ret = -EIO;
done:
	free(page);
	free(snapshot);
	result->status = ret;
	result->stage = stage;
	result->generation = expected_generation;
	result->crc_before = expected_crc32;
	result->crc_after = ret ? 0 : expected_crc32;
	result->bytes_verified = verified;
	result->target_offset = active_offset;
	result->target_size = active_size;
	return ret;
}
#endif /* !NX30_D_UBOOT_HOST_TEST */
