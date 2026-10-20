/* SPDX-License-Identifier: GPL-2.0+ */
/* Reuse the runtime IO mocks; all policy calls below execute production code. */
#define main h3c_existing_runtime_tests
#include "h3c_nand_ecc_runtime_hosttest.c"
#undef main

static unsigned int opened_windows;

/* Same boundary order as QPIC: ECC gate must pass before opening a window. */
static int attempt_window(struct mtd_info *mtd, u64 offset, u64 size)
{
	if (!h3c_nand_ecc_write_allowed(mtd, offset, size))
		return -EPERM;
	opened_windows++;
	return 0;
}

static void assert_appsbl_denied(struct mtd_info *mtd)
{
	unsigned int before = opened_windows;
	assert(attempt_window(mtd, APPSBL0, APPSBL_LEN) == -EPERM);
	assert(attempt_window(mtd, APPSBL1, APPSBL_LEN) == -EPERM);
	assert(opened_windows == before);
}

static void audit_counterexample(void)
{
	struct mock m;
	struct mtd_info mtd;
	unsigned int external_pbl, external_appsbl;
	/* These external facts have no trusted production input. Every combination
	 * must fail closed, even when ENV, system and SBL content look compatible. */
	for (external_pbl = 0; external_pbl <= 8; external_pbl += 4)
		for (external_appsbl = 0; external_appsbl <= 8; external_appsbl += 4) {
			setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
			h3c_nand_ecc_init();
			assert(h3c_nand_ecc_ready());
			assert_appsbl_denied(&mtd);
			assert(h3c_nand_ecc_boot_mode() == 0);
			assert(h3c_nand_ecc_appsbl_storage_mode() == 0);
			assert(h3c_nand_ecc_io_mode() == 4);
			/* Ordinary system transactions retain native ECC4 support. */
			assert(h3c_nand_ecc_write_allowed(&mtd, 0x900000, 0x2800000));
			printf("external_PBL=%u external_APPSBL=%u denied before window\n",
			       external_pbl, external_appsbl);
			cleanup(&m);
		}
}

static void all_catalog_profiles(void)
{
	unsigned int i, bits;
	for (i = 0; i < H3C_NAND_CATALOG_PROFILE_COUNT; i++) {
		const struct h3c_nand_catalog_profile *p = &h3c_nand_catalog_profiles[i];
		for (bits = 4; bits <= 8; bits += 4) {
			struct mock m;
			struct mtd_info mtd;
			setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
			m.profile.profile = 100U + i;
			m.profile.catalog_profile = p;
			m.profile.physical_spare = p->qpic_raw_spare_bytes;
			m.profile.nand_size = p->qpic_capacity_bytes;
			m.profile.page_size = p->page_bytes;
			m.profile.erase_size = p->erase_bytes;
			m.profile.configured_mode = bits;
			m.mode = bits; m.sys_mode = bits;
			m.env4 = bits == 4; m.env8 = bits == 8;
			h3c_nand_ecc_init();
			assert_appsbl_denied(&mtd);
			if (h3c_nand_catalog_runtime_read_qualified(p, bits))
				assert(h3c_nand_ecc_ready());
			printf("catalog=%s ECC%u denied before window ready=%d\n",
			       p->oem_model, bits, h3c_nand_ecc_ready());
			cleanup(&m);
		}
	}
}

static void incompatible_and_unknown(void)
{
	struct mock m;
	struct mtd_info mtd;
	setup(&m, &mtd, H3C_NAND_ECC_TEST_BOARD_D);
	m.sys_mode = 8;
	h3c_nand_ecc_init();
	assert(!h3c_nand_ecc_ready());
	assert_appsbl_denied(&mtd);
	m.sys_mode = 4; m.sbl_ok = 0;
	h3c_nand_ecc_init();
	assert_appsbl_denied(&mtd);
	m.profile.profile = 999;
	h3c_nand_ecc_init();
	assert(!h3c_nand_ecc_ready());
	assert_appsbl_denied(&mtd);
	cleanup(&m);
}

int main(void)
{
	audit_counterexample();
	all_catalog_profiles();
	incompatible_and_unknown();
	assert(opened_windows == 0);
	puts("D APPSBL qualification runtime gate: PASS; zero write windows");
	return 0;
}
