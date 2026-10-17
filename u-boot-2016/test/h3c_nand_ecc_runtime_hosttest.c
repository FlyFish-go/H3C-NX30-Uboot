#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h3c_nand_ecc_runtime_mock.h"
#include <h3c_nand_ecc.h>

#define ENV_OFFSET 0x480000ULL
#define ENV_SIZE   0x40000U
#define MTD_SIZE   (128ULL << 20)
#define APPSBL0    0x500000ULL
#define APPSBL1    0x640000ULL
#define APPSBL_LEN 0x140000ULL

struct mock {
	struct h3c_nand_ecc_test_profile profile;
	unsigned int mode;
	unsigned int failed;
	unsigned int failed_before;
	int env4, env8;
	int hard_read4, hard_read8;
	int badmsg4, badmsg8;
	int sys_ret;
	unsigned int sys_mode;
	int sbl_ok;
	int allow_partition;
	u8 *env;
};

static u32 crc32_test(const u8 *data, size_t len)
{
	u32 crc = 0xffffffffU;
	size_t i;
	unsigned int bit;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xedb88320U);
	}
	return ~crc;
}

static void env_make(struct mock *m)
{
	u32 crc;
	memset(m->env, 0, ENV_SIZE); /* Valid empty environment payload. */
	crc = crc32_test(m->env + 4, ENV_SIZE - 4);
	m->env[0] = (u8)crc;
	m->env[1] = (u8)(crc >> 8);
	m->env[2] = (u8)(crc >> 16);
	m->env[3] = (u8)(crc >> 24);
}

static int get_profile(void *ctx, struct h3c_nand_ecc_test_profile *p)
{
	*p = ((struct mock *)ctx)->profile;
	return 0;
}

static int set_mode(void *ctx, unsigned int mode)
{
	struct mock *m = ctx;
	if (mode != 4 && mode != 8)
		return -EINVAL;
	if (m->profile.profile == H3C_NAND_ECC_PROFILE_W25N01GWZEIG && mode != 4)
		return -EINVAL;
	m->mode = mode;
	return 0;
}

static unsigned int get_mode(void *ctx)
{
	return ((struct mock *)ctx)->mode;
}

static int get_partition(void *ctx, const char *name, unsigned int *start,
			 unsigned int *blocks)
{
	struct mock *m = ctx;
	if (!m->allow_partition || strcmp(name, "0:APPSBLENV"))
		return -ENOENT;
	*start = 0x480000U / 0x20000U;
	*blocks = 4;
	return 0;
}

static int read_mock(void *ctx, u64 offset, size_t size, size_t *retlen,
		     void *buffer)
{
	struct mock *m = ctx;
	if (offset == ENV_OFFSET && size == ENV_SIZE) {
		if ((m->mode == 4 && m->hard_read4) ||
		    (m->mode == 8 && m->hard_read8)) {
			*retlen = 0;
			return -EIO;
		}
		if ((m->mode == 4 && m->badmsg4) ||
		    (m->mode == 8 && m->badmsg8)) {
			m->failed++;
			*retlen = 0;
			return -EBADMSG;
		}
		memcpy(buffer, m->env, ENV_SIZE);
		if (!((m->mode == 4 && m->env4) || (m->mode == 8 && m->env8)))
			((u8 *)buffer)[0] ^= 1;
		*retlen = size;
		return 0;
	}
	*retlen = size;
	memset(buffer, 0xff, size);
	return 0;
}

static unsigned int ecc_failed(void *ctx)
{
	return ((struct mock *)ctx)->failed;
}

static void set_ecc_failed(void *ctx, unsigned int failed)
{
	((struct mock *)ctx)->failed = failed;
}

static int probe_system(void *ctx, unsigned int profile, unsigned int *mode)
{
	struct mock *m = ctx;
	(void)profile;
	if (m->sys_ret)
		return m->sys_ret;
	*mode = m->sys_mode;
	m->mode = m->sys_mode; /* Provider may leave its sampled mode selected. */
	return 0;
}

static int sbl1_compatible(void *ctx)
{
	return ((struct mock *)ctx)->sbl_ok;
}

static int mtd_matches(void *ctx, struct mtd_info *mtd)
{
	(void)ctx;
	return mtd && mtd->tag == 1;
}

static struct h3c_nand_ecc_test_ops ops = {
	.get_profile = get_profile,
	.set_mode = set_mode,
	.get_mode = get_mode,
	.get_partition = get_partition,
	.read = read_mock,
	.ecc_failed = ecc_failed,
	.set_ecc_failed = set_ecc_failed,
	.probe_system = probe_system,
	.sbl1_compatible = sbl1_compatible,
	.mtd_matches = mtd_matches,
};

static void setup(struct mock *m, struct mtd_info *mtd, unsigned int role)
{
	memset(m, 0, sizeof(*m));
	m->env = malloc(ENV_SIZE);
	assert(m->env);
	env_make(m);
	m->allow_partition = 1;
	m->profile.nand_size = MTD_SIZE;
	m->profile.page_size = 2048;
	m->profile.erase_size = 0x20000;
	m->profile.configured_mode = 4;
	m->profile.profile = H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG;
	m->profile.physical_spare = 128;
	m->mode = 4;
	m->env4 = 1;
	m->sys_mode = 4;
	m->failed = 9;
	m->failed_before = m->failed;
	m->sbl_ok = 1;
	mtd->tag = 1;
	h3c_nand_ecc_test_set_ops(&ops, m, mtd, role);
}

static void cleanup(struct mock *m)
{
	free(m->env);
	h3c_nand_ecc_test_reset();
}

static void test_unique4_and8(void)
{
	struct mock m;
	struct mtd_info mtd;
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 4);
	assert(h3c_nand_ecc_system_mode() == 4);
	assert(h3c_nand_ecc_io_mode() == 4);
	assert(h3c_nand_ecc_ready());
	{
		unsigned int mode = 0;
		assert(h3c_nand_ecc_autoecc_profile_mode(&mode) == 0);
		assert(mode == 0); /* Exact C8:41 keeps the legacy dual-ECC probe. */
	}
	assert(m.mode == 4 && m.failed == m.failed_before);
	/* A unique ECC8 ENV and system result also round trips to the IO mode. */
	m.env4 = 0; m.env8 = 1; m.sys_mode = 8; m.profile.configured_mode = 8;
	m.mode = 8;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 8);
	assert(h3c_nand_ecc_system_mode() == 8);
	assert(h3c_nand_ecc_io_mode() == 8);
	assert(h3c_nand_ecc_ready());
	assert(m.mode == 8 && m.failed == m.failed_before);
	cleanup(&m);
}

static void test_fixed4_and_empty(void)
{
	struct mock m;
	struct mtd_info mtd;
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_AC);
	m.profile.profile = H3C_NAND_ECC_PROFILE_W25N01GWZEIG;
	m.profile.physical_spare = 64;
	m.profile.configured_mode = 4;
	m.env8 = 1; /* Not tried on fixed-4 hardware. */
	m.sys_mode = 4;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 4);
	assert(h3c_nand_ecc_spare() == 64);
	assert(h3c_nand_ecc_ready());
	{
		unsigned int mode = 0;
		assert(h3c_nand_ecc_autoecc_profile_mode(&mode) == 1);
		assert(mode == 4);
	}
	assert(h3c_nand_ecc_write_allowed(&mtd, APPSBL0, APPSBL_LEN));
	assert(!h3c_nand_ecc_write_allowed(&mtd, APPSBL1, APPSBL_LEN - 1));
	{
		struct mtd_info other = { 0 };
	assert(!h3c_nand_ecc_write_allowed(&other, APPSBL0, APPSBL_LEN));
	}
	cleanup(&m);
}

static void test_ambiguous_corrupt_and_hard_error(void)
{
	struct mock m;
	struct mtd_info mtd;
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
	m.env8 = 1;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 0); /* Both CRC-valid is ambiguous. */
	assert(h3c_nand_ecc_system_mode() == 4); /* Independent system evidence remains visible. */
	assert(!h3c_nand_ecc_ready());
	assert(h3c_nand_ecc_io_mode() == 0);
	assert(m.mode == 4);

	m.env8 = 0; m.env4 = 0;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 0); /* Corrupt CRC in both attempts. */
	assert(!h3c_nand_ecc_ready());

	m.env4 = 1; m.hard_read8 = 1;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 0); /* Any hard read error poisons probe. */
	assert(!h3c_nand_ecc_ready());

	m.hard_read8 = 0; m.badmsg8 = 1; m.failed = m.failed_before;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 4); /* EBADMSG means no evidence. */
	assert(m.failed == m.failed_before); /* Probe restores ECC counters. */
	cleanup(&m);
}

static void test_mismatch_unknown_and_probe_hook(void)
{
	struct mock m;
	struct mtd_info mtd;
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
	m.sys_mode = 8;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_env_mode() == 4);
	assert(h3c_nand_ecc_io_mode() == 0);
	assert(!h3c_nand_ecc_ready());
	assert(m.mode == 4);

	m.profile.profile = 99;
	h3c_nand_ecc_init();
	assert(!h3c_nand_ecc_ready());
	assert(h3c_nand_ecc_spare() == 0);
	{
		unsigned int mode = 0;
		assert(h3c_nand_ecc_autoecc_profile_mode(&mode) < 0);
		assert(mode == 0);
	}

	m.profile.profile = H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG;
	m.profile.physical_spare = 128;
	m.env4 = 1; m.env8 = 0; m.sys_mode = 4;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_ready());
	m.mode = 8;
	h3c_nand_ecc_probe_complete(8);
	assert(m.mode == 4);
	assert(!h3c_nand_ecc_ready());
	cleanup(&m);
}

static void test_appsbl_sbl1_gate(void)
{
	struct mock m;
	struct mtd_info mtd;
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_B);
	m.profile.configured_mode = 8;
	m.mode = 8;
	m.env4 = 0; m.env8 = 1;
	m.sys_mode = 8;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_ready());
	m.sbl_ok = 0;
	assert(!h3c_nand_ecc_write_allowed(&mtd, APPSBL0, APPSBL_LEN));
	m.sbl_ok = 1;
	assert(h3c_nand_ecc_write_allowed(&mtd, APPSBL0, APPSBL_LEN));
	assert(h3c_nand_ecc_write_allowed(&mtd, APPSBL1, APPSBL_LEN));
	m.mode = 4;
	assert(!h3c_nand_ecc_write_allowed(&mtd, APPSBL0, APPSBL_LEN));
	cleanup(&m);
}

static void test_generic_fixed4_and_fixed8_catalog_profiles(void)
{
	struct mock m;
	struct mtd_info mtd;
	const struct h3c_nand_catalog_profile *p4 =
		h3c_nand_catalog_by_profile_id("h3c-oem-c292-128m-2k-sp64");
	const struct h3c_nand_catalog_profile *p8 =
		h3c_nand_catalog_by_profile_id("h3c-oem-2c15-128m-2k-sp128");
	assert(p4 && p8);
	assert(h3c_nand_catalog_runtime_read_qualified(p4, 4));
	assert(h3c_nand_catalog_runtime_read_qualified(p8, 8));
	assert(!h3c_nand_catalog_runtime_read_qualified(p4, 8));
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
	m.profile.profile = 100U + h3c_nand_catalog_index(p4);
	m.profile.catalog_profile = p4;
	m.profile.physical_spare = 64;
	m.profile.configured_mode = 4;
	m.env8 = 0; m.sys_mode = 4;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_ready() && h3c_nand_ecc_io_mode() == 4);
	assert(h3c_nand_ecc_select_mode(4) == 0);
	assert(h3c_nand_ecc_select_mode(8) != 0);
	/* Catalog and SBL content do not grant D APPSBL qualification. */
	m.sbl_ok = 0;
	assert(!h3c_nand_ecc_write_allowed(&mtd, APPSBL0, APPSBL_LEN));
	m.sbl_ok = 1;
	assert(!h3c_nand_ecc_write_allowed(&mtd, APPSBL0, APPSBL_LEN));
	cleanup(&m);
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
	m.profile.profile = 100U + h3c_nand_catalog_index(p8);
	m.profile.catalog_profile = p8;
	m.profile.physical_spare = 128;
	m.profile.configured_mode = 8;
	m.mode = 8; m.env4 = 0; m.env8 = 1; m.sys_mode = 8;
	h3c_nand_ecc_init();
	assert(h3c_nand_ecc_ready() && h3c_nand_ecc_io_mode() == 8);
	assert(h3c_nand_ecc_select_mode(8) == 0);
	assert(h3c_nand_ecc_select_mode(4) != 0);
	assert(!h3c_nand_ecc_write_allowed(&mtd, APPSBL0, APPSBL_LEN));
	cleanup(&m);
}

int main(void)
{
	test_unique4_and8();
	test_fixed4_and_empty();
	test_ambiguous_corrupt_and_hard_error();
	test_mismatch_unknown_and_probe_hook();
	test_appsbl_sbl1_gate();
	test_generic_fixed4_and_fixed8_catalog_profiles();
	puts("H3C NAND ECC runtime evidence/profile/write-gate tests: PASS");
	return 0;
}
