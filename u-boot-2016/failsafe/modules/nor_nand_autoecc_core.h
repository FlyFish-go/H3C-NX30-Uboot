/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NOR_NAND_AUTOECC_CORE_H__
#define __NOR_NAND_AUTOECC_CORE_H__

#ifdef NX30_NOR_AUTOECC_HOST_TEST
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include <common.h>
#endif

#define NX30_NOR_AUTOECC_ECC4 4U
#define NX30_NOR_AUTOECC_ECC8 8U
#define NX30_NOR_AUTOECC_PAGE_SIZE 2048U
#define NX30_NOR_AUTOECC_BLOCK_SIZE 0x00020000U
#define NX30_NOR_AUTOECC_SCAN_SIZE 0x08000000U
#define NX30_NOR_AUTOECC_STRIDE 0x00100000U
#define NX30_NOR_AUTOECC_SAMPLE_COUNT 128U
#define NX30_NOR_AUTOECC_MAX_EVIDENCE 8U
#define NX30_NOR_AUTOECC_TIMEOUT_MS 10000U

#define NX30_NOR_AUTOECC_STATE_UNKNOWN 0U
#define NX30_NOR_AUTOECC_STATE_ECC4 4U
#define NX30_NOR_AUTOECC_STATE_ECC8 8U
#define NX30_NOR_AUTOECC_STATE_MIXED  2U

enum nx30_nor_autoecc_reason {
	NX30_NOR_AUTOECC_REASON_NONE = 0,
	NX30_NOR_AUTOECC_REASON_INIT_FAILED,
	NX30_NOR_AUTOECC_REASON_ECC4_SWITCH_FAILED,
	NX30_NOR_AUTOECC_REASON_ECC8_SWITCH_FAILED,
	NX30_NOR_AUTOECC_REASON_READ_FAILED,
	NX30_NOR_AUTOECC_REASON_SHORT_READ,
	NX30_NOR_AUTOECC_REASON_TIMEOUT,
	NX30_NOR_AUTOECC_REASON_RESTORE_FAILED,
	NX30_NOR_AUTOECC_REASON_NO_UNIQUE_MODE,
	NX30_NOR_AUTOECC_REASON_AMBIGUOUS_BLOCK,
	NX30_NOR_AUTOECC_REASON_MIXED_BLOCKS,
};

struct nx30_nor_autoecc_page_result {
	int status;
	u32 data_len;
	u32 failed;
	u32 corrected;
};

struct nx30_nor_autoecc_ops {
	int (*init_once)(void *ctx);
	int (*set_read_ecc)(void *ctx, unsigned int bits);
	int (*read_page)(void *ctx, u64 physical_offset, void *data,
			  u32 data_capacity,
			  struct nx30_nor_autoecc_page_result *result);
	int (*restore4)(void *ctx);
	/* U-Boot get_timer(base) semantics: return elapsed ms since base. */
	u32 (*get_timer_ms)(void *ctx, u32 base);
};

struct nx30_nor_autoecc_result {
	u32 state;
	u32 selected_read_ecc;
	u32 fixed_profile_mode;
	u32 samples_ecc4;
	u32 samples_ecc8;
	u32 valid_ecc4;
	u32 valid_ecc8;
	u32 ecc_failed_ecc4;
	u32 ecc_failed_ecc8;
	u32 invalid_headers_ecc4;
	u32 invalid_headers_ecc8;
	u32 ambiguous_blocks;
	u32 retry_limit_reached;
	int error;
	enum nx30_nor_autoecc_reason reason;
	u32 evidence_ecc4_count;
	u32 evidence_ecc8_count;
	u64 evidence_ecc4[NX30_NOR_AUTOECC_MAX_EVIDENCE];
	u64 evidence_ecc8[NX30_NOR_AUTOECC_MAX_EVIDENCE];
};

void nx30_nor_autoecc_fixed_result(struct nx30_nor_autoecc_result *result,
				   unsigned int mode);
int nx30_nor_autoecc_probe(const struct nx30_nor_autoecc_ops *ops,
			   void *ctx,
			   struct nx30_nor_autoecc_result *result);
int nx30_nor_autoecc_validate_ec_header(const u8 *page, u32 *vid_hdr_offset,
					 u32 *data_offset);
int nx30_nor_autoecc_validate_vid_header(const u8 *page);

#endif
