/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __H3C_NAND_ECC_H__
#define __H3C_NAND_ECC_H__

#ifdef H3C_NAND_ECC_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint64_t u64;
#else
#include <linux/types.h>
#endif
#include <h3c_nand_catalog.h>

struct mtd_info;
unsigned int h3c_qpic_nand_physical_spare(void);

enum h3c_nand_ecc_profile {
	H3C_NAND_ECC_PROFILE_UNKNOWN = 0,
	H3C_NAND_ECC_PROFILE_GD5F1GQ5REYIG = 1,
	H3C_NAND_ECC_PROFILE_W25N01GWZEIG = 2,
};

/* Normalize the two Linux NAND ECC handoff tokens for a supported profile. */
int h3c_nand_ecc_bootargs(const char *input, char *output, size_t capacity,
			  unsigned int physical_spare, unsigned int ecc_bits);

/* Return the uniquely evidenced mode, or zero for none/ambiguous evidence. */
unsigned int h3c_nand_ecc_unique_mode(int valid4, int valid8);

/* ENV/system IO evidence and fail-closed transaction gate. These probes do
 * not qualify PBL/SBL selection or APPSBL storage/persistent installation. */
void h3c_nand_ecc_init(void);
void h3c_nand_ecc_probe_complete(unsigned int system_mode);
/* Agreed ENV/system IO mode; ready means IO consistency, not boot qualification. */
unsigned int h3c_nand_ecc_io_mode(void);
/* Independent chain evidence is currently unavailable: both return unknown (0). */
unsigned int h3c_nand_ecc_boot_mode(void);
unsigned int h3c_nand_ecc_appsbl_storage_mode(void);
unsigned int h3c_nand_ecc_env_mode(void);
unsigned int h3c_nand_ecc_system_mode(void);
unsigned int h3c_nand_ecc_spare(void);
int h3c_nand_ecc_ready(void);
/* 1: verified catalog fixed mode, 0: exact legacy C8:41 probe, -1: reject. */
int h3c_nand_ecc_autoecc_profile_mode(unsigned int *mode);
int h3c_nand_ecc_select_mode(unsigned int mode);
/* D APPSBL ranges are disabled until trusted device/chain/candidate
 * qualification is implemented. Catalog membership never grants it. */
int h3c_nand_ecc_write_allowed(struct mtd_info *mtd, u64 offset, u64 size);

struct h3c_nand_ecc_test_profile {
	unsigned int profile;
	unsigned int physical_spare;
	unsigned int configured_mode;
	u64 nand_size;
	unsigned int page_size;
	unsigned int erase_size;
	const struct h3c_nand_catalog_profile *catalog_profile;
};

#ifdef H3C_NAND_ECC_RUNTIME_HOST_TEST
struct h3c_nand_ecc_test_ops {
	void *context;
	int (*get_profile)(void *context,
			   struct h3c_nand_ecc_test_profile *profile);
	int (*set_mode)(void *context, unsigned int mode);
	unsigned int (*get_mode)(void *context);
	int (*get_partition)(void *context, const char *name,
			     unsigned int *start_blocks,
			     unsigned int *size_blocks);
	int (*read)(void *context, u64 offset, size_t size,
		     size_t *retlen, void *buffer);
	unsigned int (*ecc_failed)(void *context);
	void (*set_ecc_failed)(void *context, unsigned int failed);
	int (*probe_system)(void *context, unsigned int profile,
			    unsigned int *mode);
	int (*sbl1_compatible)(void *context);
	int (*mtd_matches)(void *context, struct mtd_info *mtd);
};

void h3c_nand_ecc_test_set_ops(const struct h3c_nand_ecc_test_ops *ops,
			       void *context, struct mtd_info *mtd,
			       unsigned int board_role);
void h3c_nand_ecc_test_reset(void);
#define H3C_NAND_ECC_TEST_BOARD_AC 1U
#define H3C_NAND_ECC_TEST_BOARD_B  2U
#define H3C_NAND_ECC_TEST_BOARD_D  3U
#endif

#endif /* __H3C_NAND_ECC_H__ */
