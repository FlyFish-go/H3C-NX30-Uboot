/* SPDX-License-Identifier: GPL-2.0 */
#ifdef NX30_NOR_AUTOECC_HOST_TEST
#include <stdint.h>
#include <errno.h>
#include <string.h>
#ifndef EUCLEAN
#define EUCLEAN 117
#endif
#else
#include <common.h>
#include <asm/errno.h>
#endif
#include "nor_nand_autoecc_core.h"

void nx30_nor_autoecc_fixed_result(struct nx30_nor_autoecc_result *result,
				   unsigned int mode)
{
	if (!result)
		return;
	memset(result, 0, sizeof(*result));
	if (mode != NX30_NOR_AUTOECC_ECC4 && mode != NX30_NOR_AUTOECC_ECC8)
		return;
	result->state = mode;
	result->selected_read_ecc = mode;
	result->fixed_profile_mode = 1U;
}

#define UBI_EC_MAGIC 0x55424923U
#define UBI_VID_MAGIC 0x55424921U
#define UBI_VERSION 1U
#define UBI_EC_HDR_SIZE_CRC 60U
#define UBI_MAX_ERASECOUNTER 0x7fffffffULL
#define UBI_VID_HDR_SIZE_CRC 60U
#define UBI_VID_DYNAMIC 1U
#define UBI_VID_STATIC 2U

static u32 get_be32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | (u32)p[3];
}

static u64 get_be64(const u8 *p)
{
	return ((u64)get_be32(p) << 32) | get_be32(p + 4);
}

/* Linux UBI crc32_le(seed=0xffffffff) returns the non-complemented CRC. */
static u32 ubi_crc32(const u8 *data, u32 len)
{
	u32 crc = 0xffffffffU;
	u32 i, bit;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
	}
	return crc;
}

int nx30_nor_autoecc_validate_ec_header(const u8 *page, u32 *vid_hdr_offset,
					 u32 *data_offset)
{
	u32 vid, data;

	if (!page || !vid_hdr_offset || !data_offset ||
	    get_be32(page) != UBI_EC_MAGIC || page[4] != UBI_VERSION ||
	    get_be64(page + 8) > UBI_MAX_ERASECOUNTER ||
	    get_be32(page + UBI_EC_HDR_SIZE_CRC) !=
	    ubi_crc32(page, UBI_EC_HDR_SIZE_CRC))
		return 0;
	vid = get_be32(page + 16);
	data = get_be32(page + 20);
	/* NX30 NAND uses 2 KiB pages and 128 KiB eraseblocks. */
	if (vid != NX30_NOR_AUTOECC_PAGE_SIZE ||
	    data != 2U * NX30_NOR_AUTOECC_PAGE_SIZE ||
	    data >= NX30_NOR_AUTOECC_BLOCK_SIZE)
		return 0;
	*vid_hdr_offset = vid;
	*data_offset = data;
	return 1;
}

int nx30_nor_autoecc_validate_vid_header(const u8 *page)
{
	u32 vol_id, vol_type;

	if (!page || get_be32(page) != UBI_VID_MAGIC || page[4] != UBI_VERSION ||
	    (page[5] != UBI_VID_DYNAMIC && page[5] != UBI_VID_STATIC) ||
	    page[6] > 1 || get_be32(page + UBI_VID_HDR_SIZE_CRC) !=
	    ubi_crc32(page, UBI_VID_HDR_SIZE_CRC))
		return 0;
	vol_id = get_be32(page + 8);
	vol_type = page[5];
	(void)vol_type;
	return vol_id != 0xffffffffU;
}

static int timeout_expired(const struct nx30_nor_autoecc_ops *ops,
			   void *ctx, u32 start)
{
	return ops->get_timer_ms(ctx, start) >= NX30_NOR_AUTOECC_TIMEOUT_MS;
}

static void add_evidence(struct nx30_nor_autoecc_result *result,
			 unsigned int ecc, u64 offset)
{
	u32 *count;
	u64 *values;

	if (ecc == NX30_NOR_AUTOECC_ECC4) {
		count = &result->evidence_ecc4_count;
		values = result->evidence_ecc4;
	} else {
		count = &result->evidence_ecc8_count;
		values = result->evidence_ecc8;
	}
	if (*count < NX30_NOR_AUTOECC_MAX_EVIDENCE)
		values[*count] = offset;
	if (*count < NX30_NOR_AUTOECC_MAX_EVIDENCE)
		(*count)++;
}

static int read_page_checked(const struct nx30_nor_autoecc_ops *ops, void *ctx,
			     u32 start, u64 offset, u8 *page,
			     struct nx30_nor_autoecc_page_result *read_result,
			     struct nx30_nor_autoecc_result *result)
{
	int ret;

	if (timeout_expired(ops, ctx, start)) {
		result->error = -ETIMEDOUT;
		result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
		return -ETIMEDOUT;
	}
	memset(read_result, 0, sizeof(*read_result));
	ret = ops->read_page(ctx, offset, page, NX30_NOR_AUTOECC_PAGE_SIZE,
			     read_result);
	if (timeout_expired(ops, ctx, start)) {
		result->error = -ETIMEDOUT;
		result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
		return -ETIMEDOUT;
	}
	/* API rc only reports rejected preconditions; MTD status is separate. */
	if (ret < 0) {
		result->error = ret;
		result->reason = NX30_NOR_AUTOECC_REASON_READ_FAILED;
		return ret;
	}
	if (read_result->status == -EBADMSG)
		/* QPIC can reject ECC before reporting any returned data. */
		return 1; /* wrong-ECC/uncorrectable page; never inspect its buffer */
	if (read_result->status == -EUCLEAN) {
		if (read_result->data_len == NX30_NOR_AUTOECC_PAGE_SIZE) {
			if (read_result->failed)
				return 1;
			return 0; /* corrected data may validate by header CRC */
		}
		result->error = -EMSGSIZE;
		result->reason = NX30_NOR_AUTOECC_REASON_SHORT_READ;
		return -EMSGSIZE;
	}
	if (read_result->status < 0) {
		result->error = read_result->status;
		result->reason = NX30_NOR_AUTOECC_REASON_READ_FAILED;
		return result->error;
	}
	if (read_result->data_len != NX30_NOR_AUTOECC_PAGE_SIZE) {
		result->error = -EMSGSIZE;
		result->reason = NX30_NOR_AUTOECC_REASON_SHORT_READ;
		return -EMSGSIZE;
	}
	if (read_result->failed)
		return 1;
	return 0;
}

static int scan_mode(const struct nx30_nor_autoecc_ops *ops, void *ctx,
		     unsigned int ecc, u32 start,
		     struct nx30_nor_autoecc_result *result,
		     u8 valid[NX30_NOR_AUTOECC_SAMPLE_COUNT])
{
	u8 page[NX30_NOR_AUTOECC_PAGE_SIZE];
	struct nx30_nor_autoecc_page_result read_result;
	u32 i, vid_offset, data_offset;
	u64 offset;
	int ret;

	if (timeout_expired(ops, ctx, start)) {
		result->error = -ETIMEDOUT;
		result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
		return -ETIMEDOUT;
	}
	ret = ops->set_read_ecc(ctx, ecc);
	if (timeout_expired(ops, ctx, start)) {
		result->error = -ETIMEDOUT;
		result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
		return -ETIMEDOUT;
	}
	if (ret) {
		result->error = ret;
		result->reason = ecc == NX30_NOR_AUTOECC_ECC4 ?
			NX30_NOR_AUTOECC_REASON_ECC4_SWITCH_FAILED :
			NX30_NOR_AUTOECC_REASON_ECC8_SWITCH_FAILED;
		return ret;
	}
	for (i = 0; i < NX30_NOR_AUTOECC_SAMPLE_COUNT; i++) {
		offset = (u64)i * NX30_NOR_AUTOECC_STRIDE;
		if (timeout_expired(ops, ctx, start)) {
			result->error = -ETIMEDOUT;
			result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
			return -ETIMEDOUT;
		}
		ret = read_page_checked(ops, ctx, start, offset, page,
					&read_result, result);
		if (ecc == NX30_NOR_AUTOECC_ECC4)
			result->samples_ecc4++;
		else
			result->samples_ecc8++;
		if (ret < 0)
			return ret;
		if (ret > 0) {
			if (ecc == NX30_NOR_AUTOECC_ECC4)
				result->ecc_failed_ecc4++;
			else
				result->ecc_failed_ecc8++;
			continue;
		}
		if (!nx30_nor_autoecc_validate_ec_header(page, &vid_offset,
						      &data_offset)) {
			if (ecc == NX30_NOR_AUTOECC_ECC4)
				result->invalid_headers_ecc4++;
			else
				result->invalid_headers_ecc8++;
			continue;
		}
		offset = (u64)i * NX30_NOR_AUTOECC_STRIDE + vid_offset;
		ret = read_page_checked(ops, ctx, start, offset, page,
					&read_result, result);
		if (ret < 0)
			return ret;
		if (ret > 0) {
			if (ecc == NX30_NOR_AUTOECC_ECC4)
				result->ecc_failed_ecc4++;
			else
				result->ecc_failed_ecc8++;
			continue;
		}
		if (!nx30_nor_autoecc_validate_vid_header(page)) {
			if (ecc == NX30_NOR_AUTOECC_ECC4)
				result->invalid_headers_ecc4++;
			else
				result->invalid_headers_ecc8++;
			continue;
		}
		valid[i] = 1;
		if (ecc == NX30_NOR_AUTOECC_ECC4) {
			result->valid_ecc4++;
			add_evidence(result, ecc, (u64)i * NX30_NOR_AUTOECC_STRIDE);
		} else {
			result->valid_ecc8++;
			add_evidence(result, ecc, (u64)i * NX30_NOR_AUTOECC_STRIDE);
		}
		(void)data_offset;
	}
	return 0;
}

static void restore_default4(const struct nx30_nor_autoecc_ops *ops,
			     void *ctx,
			     struct nx30_nor_autoecc_result *result)
{
	int ret = ops->restore4(ctx);

	result->selected_read_ecc = 0;
	if (ret) {
		if (!result->error)
			result->error = ret;
		result->reason = NX30_NOR_AUTOECC_REASON_RESTORE_FAILED;
		result->state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
	}
}

int nx30_nor_autoecc_probe(const struct nx30_nor_autoecc_ops *ops,
			   void *ctx,
			   struct nx30_nor_autoecc_result *result)
{
	u8 valid4[NX30_NOR_AUTOECC_SAMPLE_COUNT];
	u8 valid8[NX30_NOR_AUTOECC_SAMPLE_COUNT];
	u32 start, i, overlap = 0, exclusive4 = 0, exclusive8 = 0;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	result->state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
	if (!ops || !ops->init_once || !ops->set_read_ecc || !ops->read_page ||
	    !ops->restore4 || !ops->get_timer_ms)
		return -EINVAL;
	memset(valid4, 0, sizeof(valid4));
	memset(valid8, 0, sizeof(valid8));
	start = ops->get_timer_ms(ctx, 0);
	ret = ops->init_once(ctx);
	if (timeout_expired(ops, ctx, start)) {
		result->error = -ETIMEDOUT;
		result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
		restore_default4(ops, ctx, result);
		return 0;
	}
	if (ret) {
		result->error = ret;
		result->reason = NX30_NOR_AUTOECC_REASON_INIT_FAILED;
		restore_default4(ops, ctx, result);
		return 0;
	}
	ret = scan_mode(ops, ctx, NX30_NOR_AUTOECC_ECC4, start, result, valid4);
	if (!ret)
		ret = scan_mode(ops, ctx, NX30_NOR_AUTOECC_ECC8, start, result, valid8);
	if (ret) {
		result->state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
		restore_default4(ops, ctx, result);
		return 0;
	}
	for (i = 0; i < NX30_NOR_AUTOECC_SAMPLE_COUNT; i++) {
		if (valid4[i] && valid8[i])
			overlap++;
		else if (valid4[i])
			exclusive4++;
		else if (valid8[i])
			exclusive8++;
	}
	result->ambiguous_blocks = overlap;
	if (overlap) {
		result->reason = NX30_NOR_AUTOECC_REASON_AMBIGUOUS_BLOCK;
		result->state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
		restore_default4(ops, ctx, result);
		return 0;
	}
	if (result->valid_ecc4 >= 2 && !result->valid_ecc8) {
		if (timeout_expired(ops, ctx, start)) {
			result->error = -ETIMEDOUT;
			result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
			result->state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
			restore_default4(ops, ctx, result);
			return 0;
		}
		ret = ops->set_read_ecc(ctx, NX30_NOR_AUTOECC_ECC4);
		if (timeout_expired(ops, ctx, start))
			ret = -ETIMEDOUT;
		if (!ret) {
			result->state = NX30_NOR_AUTOECC_STATE_ECC4;
			result->selected_read_ecc = NX30_NOR_AUTOECC_ECC4;
			return 0;
		}
		result->error = ret;
		result->reason = NX30_NOR_AUTOECC_REASON_ECC4_SWITCH_FAILED;
	} else if (result->valid_ecc8 >= 2 && !result->valid_ecc4) {
		if (timeout_expired(ops, ctx, start)) {
			result->error = -ETIMEDOUT;
			result->reason = NX30_NOR_AUTOECC_REASON_TIMEOUT;
			result->state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
			restore_default4(ops, ctx, result);
			return 0;
		}
		ret = ops->set_read_ecc(ctx, NX30_NOR_AUTOECC_ECC8);
		if (timeout_expired(ops, ctx, start))
			ret = -ETIMEDOUT;
		if (!ret) {
			result->state = NX30_NOR_AUTOECC_STATE_ECC8;
			result->selected_read_ecc = NX30_NOR_AUTOECC_ECC8;
			return 0;
		}
		result->error = ret;
		result->reason = NX30_NOR_AUTOECC_REASON_ECC8_SWITCH_FAILED;
	} else if (result->valid_ecc4 && result->valid_ecc8 &&
		   exclusive4 && exclusive8) {
		result->state = NX30_NOR_AUTOECC_STATE_MIXED;
		result->reason = NX30_NOR_AUTOECC_REASON_MIXED_BLOCKS;
		restore_default4(ops, ctx, result);
		return 0;
	} else {
		result->reason = NX30_NOR_AUTOECC_REASON_NO_UNIQUE_MODE;
	}
	result->state = NX30_NOR_AUTOECC_STATE_UNKNOWN;
	restore_default4(ops, ctx, result);
	return 0;
}
