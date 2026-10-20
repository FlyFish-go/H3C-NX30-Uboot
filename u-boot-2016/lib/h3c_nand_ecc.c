/* SPDX-License-Identifier: GPL-2.0+ */
#if defined(CONFIG_H3C_NAND_ECC_COMPAT) || \
	defined(H3C_NAND_ECC_RUNTIME_HOST_TEST)

#ifdef H3C_NAND_ECC_RUNTIME_HOST_TEST
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "h3c_nand_ecc_runtime_mock.h"
#else
#include <common.h>
#include <malloc.h>
#include <asm/errno.h>
#include <linux/mtd/mtd.h>
#include <nand.h>
#include <asm/arch-qca-common/smem.h>
#include <qpic_nand_model.h>
#include <nx30_nor_nand.h>
#include <u-boot/sha256.h>
#include "../failsafe/modules/nor_nand_autoecc_core.h"
#endif
#include <h3c_nand_ecc.h>

#define H3C_ECC_ENV_OFFSET       0x00480000ULL
#define H3C_ECC_ENV_PART_SIZE   0x00080000U
#define H3C_ECC_ENV_COPY_SIZE   0x00040000U
#define H3C_ECC_ENV_DATA_SIZE   (H3C_ECC_ENV_COPY_SIZE - 4U)
#define H3C_ECC_SBL1_OFFSET      0x00000000ULL
#define H3C_ECC_SBL1_SIZE       0x00080000U
#define H3C_ECC_APPSBL0_OFFSET  0x00500000ULL
#define H3C_ECC_APPSBL1_OFFSET  0x00640000ULL
#define H3C_ECC_APPSBL_SIZE     0x00140000U

enum h3c_ecc_board_role {
	H3C_ECC_BOARD_UNKNOWN = 0,
	H3C_ECC_BOARD_AC,
	H3C_ECC_BOARD_B,
	H3C_ECC_BOARD_D,
};

struct h3c_ecc_runtime_state {
	struct mtd_info *mtd;
	unsigned int profile;
	unsigned int spare;
	unsigned int initial_mode;
	unsigned int env_mode;
	unsigned int io_mode;
	unsigned int system_mode;
	unsigned int board_role;
	int profile_valid;
	const struct h3c_nand_catalog_profile *catalog_profile;
	int ready;
};

static struct h3c_ecc_runtime_state ecc_state;

#ifdef H3C_NAND_ECC_RUNTIME_HOST_TEST
static struct h3c_nand_ecc_test_ops test_ops;
static unsigned int test_board_role;

void h3c_nand_ecc_test_set_ops(const struct h3c_nand_ecc_test_ops *ops,
			       void *context, struct mtd_info *mtd,
			       unsigned int board_role)
{
	if (ops) {
		test_ops = *ops;
		test_ops.context = context;
	} else {
		memset(&test_ops, 0, sizeof(test_ops));
	}
	ecc_state.mtd = mtd;
	test_board_role = board_role;
}

void h3c_nand_ecc_test_reset(void)
{
	memset(&ecc_state, 0, sizeof(ecc_state));
	memset(&test_ops, 0, sizeof(test_ops));
	test_board_role = 0;
}

static int platform_get_profile(struct h3c_nand_ecc_test_profile *p)
{
	return test_ops.get_profile ?
		test_ops.get_profile(test_ops.context, p) : -ENODEV;
}

static int platform_set_mode(unsigned int mode)
{
	return test_ops.set_mode ?
		test_ops.set_mode(test_ops.context, mode) : -ENODEV;
}

static unsigned int platform_get_mode(void)
{
	return test_ops.get_mode ? test_ops.get_mode(test_ops.context) : 0;
}

static int platform_get_partition(const char *name, unsigned int *start,
				  unsigned int *blocks)
{
	return test_ops.get_partition ?
		test_ops.get_partition(test_ops.context, name, start, blocks) : -ENODEV;
}

static int platform_read(u64 offset, size_t size, size_t *retlen, void *buffer)
{
	return test_ops.read ?
		test_ops.read(test_ops.context, offset, size, retlen, buffer) : -ENODEV;
}

static unsigned int platform_ecc_failed(void)
{
	return test_ops.ecc_failed ? test_ops.ecc_failed(test_ops.context) : 0;
}

static int platform_system_probe(unsigned int profile, unsigned int *mode)
{
	return test_ops.probe_system ?
		test_ops.probe_system(test_ops.context, profile, mode) : -ENODEV;
}

static int platform_sbl1_compatible(void)
{
	return test_ops.sbl1_compatible ?
		test_ops.sbl1_compatible(test_ops.context) : 0;
}

static int platform_mtd_matches(struct mtd_info *mtd)
{
	return test_ops.mtd_matches ?
		test_ops.mtd_matches(test_ops.context, mtd) : 0;
}

static unsigned int platform_board_role(void)
{
	return test_board_role;
}

static void platform_save_ecc_stats(unsigned int *failed)
{
	*failed = platform_ecc_failed();
}

static void platform_restore_ecc_stats(unsigned int failed)
{
	if (test_ops.set_ecc_failed)
		test_ops.set_ecc_failed(test_ops.context, failed);
}

static u64 platform_mtd_size(void)
{
	struct h3c_nand_ecc_test_profile p;
	return platform_get_profile(&p) ? 0 : p.nand_size;
}

#else /* U-Boot platform */

extern unsigned int h3c_qpic_nand_physical_spare(void);
extern int qpic_nand_get_model_info(struct qpic_nand_model_info *info);
extern int nx30_nor_nand_autoecc_get_result(
		bool retry, struct nx30_nor_autoecc_result *result);

static int platform_get_profile(struct h3c_nand_ecc_test_profile *p)
{
	struct qpic_nand_model_info model;
	struct mtd_info *mtd = &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
	unsigned int spare;

	memset(&model, 0, sizeof(model));
	if (qpic_nand_get_model_info(&model) || !mtd || !mtd->name ||
	    !(spare = h3c_qpic_nand_physical_spare()))
		return -ENODEV;
	memset(p, 0, sizeof(*p));
	p->profile = H3C_NAND_ECC_PROFILE_UNKNOWN;
	p->physical_spare = spare;
	p->configured_mode = mtd->ecc_strength;
	p->nand_size = mtd->size;
	p->page_size = mtd->writesize;
	p->erase_size = mtd->erasesize;
	/* A source-bound build may disambiguate identical JEDEC prefixes, but
	 * must still match the complete runtime QPIC tuple. Empty means legacy
	 * catalog matching, which itself fails closed on ambiguous tuples.
	 */
#ifdef CONFIG_H3C_NAND_PROFILE_ID
	if (CONFIG_H3C_NAND_PROFILE_ID[0]) {
		const struct h3c_nand_catalog_profile *expected =
			h3c_nand_catalog_by_profile_id(CONFIG_H3C_NAND_PROFILE_ID);
		if (expected && !strcmp(expected->qpic_model, model.name) &&
		    expected->id[0] == model.id[0] &&
		    expected->id[1] == model.id[1] &&
		    expected->qpic_capacity_bytes == (unsigned int)mtd->size &&
		    expected->page_bytes == mtd->writesize &&
		    expected->erase_bytes == mtd->erasesize &&
		    expected->qpic_raw_spare_bytes == spare &&
		    (mtd->ecc_strength == 4 || mtd->ecc_strength == 8) &&
		    h3c_nand_catalog_runtime_read_qualified(expected,
							mtd->ecc_strength) &&
		    (mtd->ecc_strength == 4 ?
		     expected->qpic_mtd_oob_ecc4 :
		     expected->qpic_mtd_oob_ecc8) == mtd->oobsize)
			p->catalog_profile = expected;
	} else
#endif
		p->catalog_profile = h3c_nand_catalog_match_qpic_model(
			model.id, model.name, (unsigned int)mtd->size,
			mtd->writesize, mtd->erasesize, spare,
			mtd->ecc_strength, mtd->oobsize);
	if (!h3c_nand_catalog_runtime_read_qualified(p->catalog_profile,
						    p->configured_mode))
		return 0;
	if (model.id[0] == 0xc8 && model.id[1] == 0x41 &&
	    !strcmp(model.name, "GD5F1GQ5REYIG"))
		p->profile = H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG;
	else if (model.id[0] == 0xef && model.id[1] == 0xba &&
		 model.id[2] == 0x21 && !strcmp(model.name, "W25N01GWZEIG"))
		p->profile = H3C_NAND_ECC_PROFILE_W25N01GWZEIG;
	else
		p->profile = 100U + h3c_nand_catalog_index(p->catalog_profile);
	return 0;
}

static int platform_set_mode(unsigned int mode)
{
	if (ecc_state.profile == H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG)
		return nx30_nor_nand_set_read_ecc(mode);
	if (!h3c_nand_catalog_runtime_read_qualified(ecc_state.catalog_profile,
						    mode))
		return -ENODEV;
	/* Fixed BCH profiles never use the C8:41 dual-mode register path. */
	return ecc_state.mtd && ecc_state.mtd->ecc_strength == mode ? 0 : -EINVAL;
}

static unsigned int platform_get_mode(void)
{
	return ecc_state.mtd ? ecc_state.mtd->ecc_strength : 0;
}

static int platform_get_partition(const char *name, unsigned int *start,
				  unsigned int *blocks)
{
	return smem_getpart((char *)name, start, blocks);
}

static int platform_read(u64 offset, size_t size, size_t *retlen, void *buffer)
{
	return mtd_read(ecc_state.mtd, offset, size, retlen, buffer);
}

static unsigned int platform_ecc_failed(void)
{
	return ecc_state.mtd ? ecc_state.mtd->ecc_stats.failed : 0;
}

static int platform_mtd_matches(struct mtd_info *mtd)
{
	return mtd && mtd == ecc_state.mtd;
}

static u64 platform_mtd_size(void)
{
	return ecc_state.mtd ? ecc_state.mtd->size : 0;
}

static void platform_save_ecc_stats(struct mtd_ecc_stats *stats)
{
	*stats = ecc_state.mtd->ecc_stats;
}

static void platform_restore_ecc_stats(const struct mtd_ecc_stats *stats)
{
	ecc_state.mtd->ecc_stats = *stats;
}

static unsigned int platform_board_role(void)
{
#if defined(CONFIG_TARGET_IPQ5018_H3C_HW_A) || \
	defined(CONFIG_TARGET_IPQ5018_H3C_HW_C)
	return H3C_ECC_BOARD_AC;
#elif defined(CONFIG_TARGET_IPQ5018_H3C_RT3000_B)
	return H3C_ECC_BOARD_B;
#elif defined(CONFIG_TARGET_IPQ5018_H3C_HW_D)
	return H3C_ECC_BOARD_D;
#else
	return H3C_ECC_BOARD_UNKNOWN;
#endif
}

static int h3c_nand_ecc_probe_fixed_system(unsigned int *mode);

static int platform_system_probe(unsigned int profile, unsigned int *mode)
{
	if (profile == H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG) {
		struct nx30_nor_autoecc_result result;
		int ret = nx30_nor_nand_autoecc_get_result(false, &result);

		if (ret || result.error || result.state == NX30_NOR_AUTOECC_STATE_MIXED ||
		    (result.state != NX30_NOR_AUTOECC_STATE_ECC4 &&
		     result.state != NX30_NOR_AUTOECC_STATE_ECC8))
			return -EIO;
		if ((result.state == NX30_NOR_AUTOECC_STATE_ECC4 &&
		     (result.valid_ecc4 < 2U || result.valid_ecc8)) ||
		    (result.state == NX30_NOR_AUTOECC_STATE_ECC8 &&
		     (result.valid_ecc8 < 2U || result.valid_ecc4)))
			return -EIO;
		*mode = result.selected_read_ecc;
		return 0;
	}
	if (ecc_state.catalog_profile)
		return h3c_nand_ecc_probe_fixed_system(mode);
	return -ENODEV;
}

static int h3c_nand_ecc_read_page(u64 offset, u8 *page)
{
	size_t retlen = 0;
	unsigned int failed_before = platform_ecc_failed();
	unsigned int failed_after;
	int ret = platform_read(offset, 2048U, &retlen, page);

	failed_after = platform_ecc_failed();
	if ((ret && ret != -EUCLEAN) || retlen != 2048U ||
	    failed_after != failed_before)
		return ret ? ret : -EIO;
	return 0;
}

static int h3c_nand_ecc_probe_fixed_system(unsigned int *mode)
{
	u8 ec[2048], vid[2048];
	u32 vid_offset, data_offset;
	unsigned int i, valid = 0, current = platform_get_mode();
	unsigned int mib_count;
	int ret;

	if (!mode || !h3c_nand_catalog_runtime_read_qualified(ecc_state.catalog_profile, current) ||
	    !ecc_state.catalog_profile ||
	    platform_get_mode() != ecc_state.initial_mode)
		return -ENODEV;
	mib_count = ecc_state.catalog_profile->capacity_bytes >> 20;
	for (i = 0; i < mib_count; i++) {
		u64 base = (u64)i * (1U << 20);

		ret = h3c_nand_ecc_read_page(base, ec);
		if (ret)
			return ret;
		if (!nx30_nor_autoecc_validate_ec_header(ec, &vid_offset,
							 &data_offset))
			continue;
		if (vid_offset >= ecc_state.catalog_profile->erase_bytes ||
		    (vid_offset & (ecc_state.catalog_profile->page_bytes - 1U)) ||
		    base + vid_offset >= ecc_state.catalog_profile->capacity_bytes)
			continue;
		ret = h3c_nand_ecc_read_page(base + vid_offset, vid);
		if (ret)
			return ret;
		if (nx30_nor_autoecc_validate_vid_header(vid))
			valid++;
		(void)data_offset;
	}
	if (valid < 2U)
		return -ENODATA;
	*mode = current;
	return 0;
}

static int platform_sbl1_compatible(void)
{
	static const u8 expected[32] = {
		0x84,0x2f,0x88,0x53,0xec,0x46,0xe3,0xf6,
		0x9c,0x7c,0xe4,0x52,0x80,0x01,0x11,0x7b,
		0x9c,0x2c,0x78,0x86,0x4d,0xe2,0x54,0xdc,
		0x99,0xdc,0x61,0x7f,0x59,0xea,0x5e,0x21,
	};
	u8 actual[32];
	u8 *buffer;
	size_t retlen = 0;
	unsigned int failed_before, failed_after;
	int ret;

	if (!ecc_state.mtd || ecc_state.io_mode != platform_get_mode())
		return 0;
	buffer = memalign(ARCH_DMA_MINALIGN, H3C_ECC_SBL1_SIZE);
	if (!buffer)
		return 0;
	failed_before = platform_ecc_failed();
	ret = platform_read(H3C_ECC_SBL1_OFFSET, H3C_ECC_SBL1_SIZE,
			    &retlen, buffer);
	failed_after = platform_ecc_failed();
	if ((ret && ret != -EUCLEAN) || retlen != H3C_ECC_SBL1_SIZE ||
	    failed_after != failed_before) {
		free(buffer);
		return 0;
	}
	sha256_csum_wd(buffer, H3C_ECC_SBL1_SIZE, actual, CHUNKSZ_SHA256);
	free(buffer);
	return !memcmp(actual, expected, sizeof(actual));
}
#endif

static u32 h3c_nand_ecc_crc32(const u8 *data, size_t length)
{
	u32 crc = 0xffffffffU;
	size_t i;
	unsigned int bit;

	for (i = 0; i < length; i++) {
		crc ^= data[i];
		for (bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^
			      ((0U - (crc & 1U)) & 0xedb88320U);
	}
	return ~crc;
}

static u32 h3c_nand_ecc_get_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) |
	       ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int h3c_nand_ecc_read_env_mode(u8 *env, unsigned int mode,
				     unsigned int *valid, int *hard_error)
{
	size_t retlen = 0;
	unsigned int failed_before, failed_after;
	int ret;

	*valid = 0;
	failed_before = platform_ecc_failed();
	if (ecc_state.profile == H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG)
		ret = platform_set_mode(mode);
	else
		ret = platform_set_mode(mode);
	if (ret) {
		*hard_error = 1;
		return ret;
	}
	failed_before = platform_ecc_failed();
	ret = platform_read(H3C_ECC_ENV_OFFSET, H3C_ECC_ENV_COPY_SIZE,
			    &retlen, env);
	failed_after = platform_ecc_failed();
	if (ret == -EBADMSG)
		return 0;
	if ((ret && ret != -EUCLEAN) || retlen != H3C_ECC_ENV_COPY_SIZE ||
	    failed_after != failed_before) {
		*hard_error = 1;
		return ret ? ret : -EIO;
	}
	if (h3c_nand_ecc_get_le32(env) ==
	    h3c_nand_ecc_crc32(env + 4, H3C_ECC_ENV_DATA_SIZE))
		*valid = 1;
	return 0;
}

static unsigned int h3c_nand_ecc_detect_env(void)
{
	u8 *env;
	unsigned int start_blocks = 0, size_blocks = 0;
	unsigned int valid4 = 0, valid8 = 0, valid;
	unsigned int modes[2] = { 4U, 8U };
	unsigned int count, i;
	int hard_error = 0;

	if (!ecc_state.profile_valid ||
	    platform_get_partition("0:APPSBLENV", &start_blocks, &size_blocks) ||
	    (u64)start_blocks * (128U << 10) != H3C_ECC_ENV_OFFSET ||
	    (u64)size_blocks * (128U << 10) != H3C_ECC_ENV_PART_SIZE)
		return 0;
#ifdef H3C_NAND_ECC_RUNTIME_HOST_TEST
	env = malloc(H3C_ECC_ENV_COPY_SIZE);
#else
	env = memalign(ARCH_DMA_MINALIGN, H3C_ECC_ENV_COPY_SIZE);
#endif
	if (!env)
		return 0;
	count = ecc_state.profile == H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG ?
		2U : 1U;
	if (count == 1U)
		modes[0] = ecc_state.initial_mode;
	for (i = 0; i < count; i++) {
		int ret = h3c_nand_ecc_read_env_mode(env, modes[i], &valid,
						      &hard_error);

		if (!ret && valid) {
			if (modes[i] == 4U)
				valid4 = 1;
			else
				valid8 = 1;
		}
	}
	free(env);
	if (hard_error)
		return 0;
	return h3c_nand_ecc_unique_mode(valid4, valid8);
}

static int h3c_nand_ecc_profile_matches(
		const struct h3c_nand_ecc_test_profile *p)
{
	if (!p)
		return 0;
	if (p->catalog_profile)
		return p->nand_size == p->catalog_profile->qpic_capacity_bytes &&
		       p->page_size == p->catalog_profile->page_bytes &&
		       p->erase_size == p->catalog_profile->erase_bytes &&
		       p->physical_spare == p->catalog_profile->qpic_raw_spare_bytes &&
		       h3c_nand_catalog_runtime_read_qualified(p->catalog_profile,
							 p->configured_mode);
	if (p->nand_size != (128ULL << 20) ||
	    p->page_size != 2048U || p->erase_size != (128U << 10))
		return 0;
	if (p->profile == H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG)
		return p->physical_spare == 128U &&
		       (p->configured_mode == 4U || p->configured_mode == 8U);
	if (p->profile == H3C_NAND_ECC_PROFILE_W25N01GWZEIG)
		return p->physical_spare == 64U && p->configured_mode == 4U;
	return 0;
}

static void h3c_nand_ecc_update_ready(void)
{
	ecc_state.ready = ecc_state.profile_valid && ecc_state.env_mode &&
		ecc_state.io_mode && ecc_state.system_mode &&
		ecc_state.io_mode == ecc_state.env_mode &&
		ecc_state.io_mode == ecc_state.system_mode &&
		platform_get_mode() == ecc_state.io_mode;
}

void h3c_nand_ecc_probe_complete(unsigned int system_mode)
{
	if (!ecc_state.profile_valid ||
	    !h3c_nand_catalog_runtime_read_qualified(ecc_state.catalog_profile,
						    system_mode)) {
		ecc_state.system_mode = 0;
	} else {
		ecc_state.system_mode = system_mode;
	}
	if (ecc_state.io_mode)
		(void)platform_set_mode(ecc_state.io_mode);
	else if (ecc_state.initial_mode)
		(void)platform_set_mode(ecc_state.initial_mode);
	h3c_nand_ecc_update_ready();
}

void h3c_nand_ecc_init(void)
{
	struct h3c_nand_ecc_test_profile p;
	unsigned int mode;
	int ret;
#ifdef H3C_NAND_ECC_RUNTIME_HOST_TEST
	unsigned int saved_failed;
#else
	struct mtd_ecc_stats saved_stats;
#endif

#ifdef H3C_NAND_ECC_RUNTIME_HOST_TEST
	{
		struct mtd_info *test_mtd = ecc_state.mtd;
		memset(&ecc_state, 0, sizeof(ecc_state));
		ecc_state.mtd = test_mtd;
		ecc_state.board_role = platform_board_role();
	}
#else
	memset(&ecc_state, 0, sizeof(ecc_state));
	ecc_state.mtd = &nand_info[CONFIG_QPIC_NAND_NAND_INFO_IDX];
	ecc_state.board_role = platform_board_role();
#endif
	ret = platform_get_profile(&p);
	if (ret || !h3c_nand_ecc_profile_matches(&p))
		goto unknown;
	ecc_state.profile = p.profile;
	ecc_state.catalog_profile = p.catalog_profile;
#ifdef H3C_NAND_ECC_RUNTIME_HOST_TEST
	if (!ecc_state.catalog_profile) {
		if (p.profile == H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG)
			ecc_state.catalog_profile = h3c_nand_catalog_by_profile_id(
				"h3c-oem-c841-128m-2k-sp128");
		else if (p.profile == H3C_NAND_ECC_PROFILE_W25N01GWZEIG)
			ecc_state.catalog_profile = h3c_nand_catalog_by_profile_id(
				"h3c-oem-efba-128m-2k-sp64");
	}
#endif
	ecc_state.spare = p.physical_spare;
	ecc_state.initial_mode = p.configured_mode;
	ecc_state.profile_valid = 1;
#ifndef H3C_NAND_ECC_RUNTIME_HOST_TEST
	platform_save_ecc_stats(&saved_stats);
#else
	platform_save_ecc_stats(&saved_failed);
#endif
	ecc_state.env_mode = h3c_nand_ecc_detect_env();
	/* This is an operating mode for ENV/system IO only, never boot evidence. */
	ecc_state.io_mode = ecc_state.env_mode;
	mode = ecc_state.io_mode ? ecc_state.io_mode : ecc_state.initial_mode;
	if (!mode || platform_set_mode(mode)) {
		ecc_state.env_mode = 0;
		ecc_state.io_mode = 0;
		goto restore_unknown;
	}
	{
		unsigned int system_mode = 0;
		ret = platform_system_probe(ecc_state.profile, &system_mode);
		if (!ret)
			ecc_state.system_mode = system_mode;
	}
	mode = ecc_state.io_mode ? ecc_state.io_mode : ecc_state.initial_mode;
	if (mode)
		(void)platform_set_mode(mode);
#ifndef H3C_NAND_ECC_RUNTIME_HOST_TEST
	platform_restore_ecc_stats(&saved_stats);
#else
	platform_restore_ecc_stats(saved_failed);
#endif
	h3c_nand_ecc_update_ready();
#ifndef H3C_NAND_ECC_RUNTIME_HOST_TEST
	printf("H3C NAND ECC: ENV=%u system=%u ready=%d\n", ecc_state.env_mode,
	       ecc_state.system_mode, ecc_state.ready);
#endif
	return;

restore_unknown:
	if (ecc_state.initial_mode)
		(void)platform_set_mode(ecc_state.initial_mode);
#ifndef H3C_NAND_ECC_RUNTIME_HOST_TEST
	platform_restore_ecc_stats(&saved_stats);
#else
	platform_restore_ecc_stats(saved_failed);
#endif
unknown:
	ecc_state.env_mode = 0;
	ecc_state.io_mode = 0;
	ecc_state.system_mode = 0;
	ecc_state.ready = 0;
}

unsigned int h3c_nand_ecc_io_mode(void)
{
	return h3c_nand_ecc_ready() ? ecc_state.io_mode : 0;
}

unsigned int h3c_nand_ecc_boot_mode(void)
{
	/* ENV/system CRCs cannot establish the actual PBL/SBL runtime selection.
	 * No trusted platform provider currently supplies that independent fact. */
	return 0;
}

unsigned int h3c_nand_ecc_appsbl_storage_mode(void)
{
	/* Neither ENV nor sampled system metadata establishes APPSBL storage. */
	return 0;
}

unsigned int h3c_nand_ecc_env_mode(void)
{
	return ecc_state.env_mode;
}

unsigned int h3c_nand_ecc_system_mode(void)
{
	return ecc_state.system_mode;
}

unsigned int h3c_nand_ecc_spare(void)
{
	return ecc_state.profile_valid ? ecc_state.spare : 0;
}

int h3c_nand_ecc_ready(void)
{
	h3c_nand_ecc_update_ready();
	return ecc_state.ready;
}

int h3c_nand_ecc_autoecc_profile_mode(unsigned int *mode)
{
	if (!mode)
		return -1;
	*mode = 0;
	if (ecc_state.profile == H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG)
		return 0;
	if (!ecc_state.profile_valid || !ecc_state.catalog_profile ||
	    !h3c_nand_ecc_ready() || !ecc_state.system_mode ||
	    ecc_state.system_mode != ecc_state.io_mode ||
	    !h3c_nand_catalog_runtime_read_qualified(
		    ecc_state.catalog_profile, ecc_state.system_mode))
		return -1;
	*mode = ecc_state.system_mode;
	return 1;
}

int h3c_nand_ecc_select_mode(unsigned int mode)
{
	if (!h3c_nand_ecc_ready() || mode != ecc_state.io_mode ||
	    platform_set_mode(mode))
		return -EIO;
	h3c_nand_ecc_update_ready();
	return ecc_state.ready ? 0 : -EIO;
}

static int h3c_nand_ecc_is_appsbl(u64 offset, u64 size)
{
	const u64 starts[] = { H3C_ECC_APPSBL0_OFFSET, H3C_ECC_APPSBL1_OFFSET };
	unsigned int i;

	for (i = 0; i < 2U; i++) {
		u64 end = offset + size;
		u64 part_end = starts[i] + H3C_ECC_APPSBL_SIZE;

		if (offset < part_end && starts[i] < end)
			return offset == starts[i] && size == H3C_ECC_APPSBL_SIZE ?
				1 : -1;
	}
	return 0;
}

int h3c_nand_ecc_write_allowed(struct mtd_info *mtd, u64 offset, u64 size)
{
	int appsbl;

	if (!h3c_nand_ecc_ready() || !platform_mtd_matches(mtd) || !size ||
	    offset > platform_mtd_size() || size > platform_mtd_size() - offset ||
	    platform_get_mode() != ecc_state.io_mode ||
	    ecc_state.io_mode != ecc_state.system_mode)
		return 0;
	appsbl = h3c_nand_ecc_is_appsbl(offset, size);
	if (appsbl < 0)
		return 0;
	if (appsbl) {
		if (ecc_state.board_role == H3C_ECC_BOARD_AC &&
		    (ecc_state.profile != H3C_NAND_ECC_PROFILE_W25N01GWZEIG ||
		     ecc_state.io_mode != 4U || !platform_sbl1_compatible()))
			return 0;
		if (ecc_state.board_role == H3C_ECC_BOARD_B &&
		    (ecc_state.profile != H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG ||
		     ecc_state.io_mode != 8U || !platform_sbl1_compatible()))
			return 0;
		/* D APPSBL self-update is unavailable: there is no trusted source for
		 * the actual preloader selection, APPSBL storage format AND this
		 * device/candidate's installation qualification. Catalog rows, SBL
		 * hashes and ENV/system agreement do not supply those facts. Do not
		 * replace this lock with a model, hostname or ENV allowlist. Reads
		 * and non-APPSBL transactions continue to use the IO-mode evidence.
		 * This gate is called before QPIC opens a persistent write window. */
		if (ecc_state.board_role == H3C_ECC_BOARD_D)
			return 0;
		if (ecc_state.board_role != H3C_ECC_BOARD_AC &&
		    ecc_state.board_role != H3C_ECC_BOARD_B &&
		    ecc_state.board_role != H3C_ECC_BOARD_D)
			return 0;
	}
	return 1;
}

#endif /* CONFIG_H3C_NAND_ECC_COMPAT || host test */
